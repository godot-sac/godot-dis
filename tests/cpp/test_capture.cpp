// C++ unit tests for src/record/capture_format (pcapng / pcap / pdulog), no Godot needed.
// Build and run: scons tests (see docs/building.md).
#include <doctest.h>

#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "record/capture_format.h"

using namespace dis::capture;

namespace {

// A minimal valid-looking DIS PDU: version 7, given type, length field = size.
std::vector<uint8_t> pdu(uint8_t type, size_t size, uint8_t fill = 0xAB) {
	std::vector<uint8_t> out(size, fill);
	out[0] = 7;
	out[1] = 1;
	out[2] = type;
	out[3] = 1;
	out[8] = static_cast<uint8_t>(size >> 8);
	out[9] = static_cast<uint8_t>(size);
	return out;
}

void be16(std::vector<uint8_t> &out, uint16_t v) {
	out.push_back(static_cast<uint8_t>(v >> 8));
	out.push_back(static_cast<uint8_t>(v));
}

void le32(std::vector<uint8_t> &out, uint32_t v) {
	for (int i = 0; i < 4; i++) {
		out.push_back(static_cast<uint8_t>(v >> (8 * i)));
	}
}

void be32(std::vector<uint8_t> &out, uint32_t v) {
	for (int i = 3; i >= 0; i--) {
		out.push_back(static_cast<uint8_t>(v >> (8 * i)));
	}
}

// IPv4 + UDP around a payload (checksums left 0: readers don't check them).
std::vector<uint8_t> ipv4_udp(const std::vector<uint8_t> &payload, uint16_t source_port = 3000,
		uint16_t destination_port = 3000, uint16_t fragment = 0) {
	std::vector<uint8_t> out = { 0x45, 0 };
	be16(out, static_cast<uint16_t>(28 + payload.size()));
	be16(out, 0);
	be16(out, fragment);
	out.insert(out.end(), { 64, 17, 0, 0, 10, 0, 0, 5, 239, 1, 2, 3 });
	be16(out, source_port);
	be16(out, destination_port);
	be16(out, static_cast<uint16_t>(8 + payload.size()));
	be16(out, 0);
	out.insert(out.end(), payload.begin(), payload.end());
	return out;
}

std::vector<uint8_t> ipv6_udp(const std::vector<uint8_t> &payload) {
	std::vector<uint8_t> out = { 0x60, 0, 0, 0 };
	be16(out, static_cast<uint16_t>(8 + payload.size()));
	out.insert(out.end(), { 17, 64 });
	for (int i = 0; i < 15; i++) {
		out.push_back(0);
	}
	out.push_back(1); // source ::1
	out.insert(out.end(), { 0xFF, 0x02 });
	for (int i = 0; i < 13; i++) {
		out.push_back(0);
	}
	out.push_back(1); // destination ff02::1
	be16(out, 3000);
	be16(out, 3000);
	be16(out, static_cast<uint16_t>(8 + payload.size()));
	be16(out, 0);
	out.insert(out.end(), payload.begin(), payload.end());
	return out;
}

std::vector<uint8_t> ethernet(const std::vector<uint8_t> &ip, uint16_t ethertype = 0x0800, bool vlan = false) {
	std::vector<uint8_t> out(12, 0x11);
	if (vlan) {
		be16(out, 0x8100);
		be16(out, 42);
	}
	be16(out, ethertype);
	out.insert(out.end(), ip.begin(), ip.end());
	return out;
}

// A classic pcap file with one link type; `big_endian` and `nanoseconds` select the header variant.
std::vector<uint8_t> pcap_file(uint32_t link_type, const std::vector<std::vector<uint8_t>> &frames, bool big_endian,
		bool nanoseconds) {
	auto put = [&](std::vector<uint8_t> &out, uint32_t v) { big_endian ? be32(out, v) : le32(out, v); };
	auto put16 = [&](std::vector<uint8_t> &out, uint16_t v) {
		if (big_endian) {
			be16(out, v);
		} else {
			out.push_back(static_cast<uint8_t>(v));
			out.push_back(static_cast<uint8_t>(v >> 8));
		}
	};
	std::vector<uint8_t> out;
	put(out, nanoseconds ? 0xA1B23C4D : 0xA1B2C3D4);
	put16(out, 2);
	put16(out, 4);
	put(out, 0);
	put(out, 0);
	put(out, 65535);
	put(out, link_type);
	for (size_t i = 0; i < frames.size(); i++) {
		put(out, 1700000000 + static_cast<uint32_t>(i));
		put(out, nanoseconds ? 250000000 : 250000);
		put(out, static_cast<uint32_t>(frames[i].size()));
		put(out, static_cast<uint32_t>(frames[i].size()));
		out.insert(out.end(), frames[i].begin(), frames[i].end());
	}
	return out;
}

std::vector<Packet> read_all(const std::vector<uint8_t> &file, ReadStats &stats, std::string &error, int port = 0) {
	std::vector<Packet> packets;
	read(file.data(), file.size(), Format::UNKNOWN, packets, error, stats, port);
	return packets;
}

std::vector<uint8_t> sample_pcapng(std::vector<Packet> &packets) {
	Packet a;
	a.time = 1793534400.123456;
	a.data = pdu(1, 144);
	a.source_ip = "192.168.1.20";
	a.source_port = 54321;
	a.destination_ip = "239.1.2.3";
	a.destination_port = 3000;
	Packet b = a;
	b.time += 0.5;
	b.data = pdu(2, 96, 0x01);
	b.destination_ip = "192.168.1.255";
	Packet c = a;
	c.time += 1.25;
	c.data = pdu(3, 104 + 16, 0x7F);
	c.destination_ip = "10.0.0.7";
	c.destination_port = 3001;
	packets = { a, b, c };
	std::vector<uint8_t> file = pcapng_file_header("godot-dis tests");
	for (const Packet &p : packets) {
		const std::vector<uint8_t> record = pcapng_packet_record(p);
		file.insert(file.end(), record.begin(), record.end());
	}
	return file;
}

} // namespace

TEST_CASE("pcapng round trip") {
	std::vector<Packet> written;
	const std::vector<uint8_t> file = sample_pcapng(written);
	CHECK(detect(file.data(), file.size()) == Format::PCAPNG);
	ReadStats stats;
	std::string error;
	const std::vector<Packet> read_back = read_all(file, stats, error);
	CHECK(error.empty());
	REQUIRE(read_back.size() == written.size());
	CHECK(stats.frames == 3);
	for (size_t i = 0; i < written.size(); i++) {
		CHECK(read_back[i].data == written[i].data);
		CHECK(read_back[i].source_ip == written[i].source_ip);
		CHECK(read_back[i].source_port == written[i].source_port);
		CHECK(read_back[i].destination_ip == written[i].destination_ip);
		CHECK(read_back[i].destination_port == written[i].destination_port);
		CHECK(read_back[i].time == doctest::Approx(written[i].time).epsilon(1e-12));
		CHECK(std::abs(read_back[i].time - written[i].time) < 1e-6);
	}
}

TEST_CASE("pcapng frames carry valid checksums and standard MAC addresses") {
	std::vector<Packet> written;
	const std::vector<uint8_t> file = sample_pcapng(written);
	// First packet record starts after the section header and interface blocks.
	const std::vector<uint8_t> header = pcapng_file_header("godot-dis tests");
	const std::vector<uint8_t> record = pcapng_packet_record(written[0]);
	CHECK(std::equal(record.begin(), record.end(), file.begin() + static_cast<std::ptrdiff_t>(header.size())));
	const uint8_t *frame = record.data() + 28; // block header 8 + EPB fields 20
	const uint8_t multicast_mac[] = { 0x01, 0x00, 0x5E, 0x01, 0x02, 0x03 };
	CHECK(std::memcmp(frame, multicast_mac, 6) == 0);
	CHECK(internet_checksum(frame + 14, 20) == 0); // a valid IPv4 header sums to zero
	const uint8_t *ip = frame + 14;
	const size_t udp_length = (ip[24] << 8) | ip[25];
	const uint32_t pseudo = ((ip[12] << 8) | ip[13]) + ((ip[14] << 8) | ip[15]) + ((ip[16] << 8) | ip[17]) +
			((ip[18] << 8) | ip[19]) + 17 + static_cast<uint32_t>(udp_length);
	CHECK(internet_checksum(ip + 20, udp_length, pseudo) == 0);

	const std::vector<uint8_t> broadcast = pcapng_packet_record(written[1]);
	CHECK(std::all_of(broadcast.begin() + 28, broadcast.begin() + 34, [](uint8_t b) { return b == 0xFF; }));
}

TEST_CASE("pcapng with a big-endian section and nanosecond timestamps") {
	std::vector<uint8_t> file;
	// Section header block, big-endian.
	be32(file, 0x0A0D0D0A);
	be32(file, 28);
	be32(file, 0x1A2B3C4D);
	be16(file, 1);
	be16(file, 0);
	be32(file, 0xFFFFFFFF);
	be32(file, 0xFFFFFFFF);
	be32(file, 28);
	// Interface: Ethernet, if_tsresol = 9 (nanoseconds), if_tsoffset = 100 s.
	be32(file, 1);
	be32(file, 20 + 8 + 12 + 4);
	be16(file, 1);
	be16(file, 0);
	be32(file, 0);
	be16(file, 9);
	be16(file, 1);
	file.insert(file.end(), { 9, 0, 0, 0 });
	be16(file, 14);
	be16(file, 8);
	be32(file, 0);
	be32(file, 100);
	be32(file, 0); // end of options
	be32(file, 20 + 8 + 12 + 4);
	// Enhanced packet block.
	const std::vector<uint8_t> frame = ethernet(ipv4_udp(pdu(1, 144)));
	const uint32_t padded = static_cast<uint32_t>((frame.size() + 3) & ~size_t(3));
	const uint64_t stamp = 1500000000ull * 1000000000ull + 5;
	be32(file, 6);
	be32(file, 32 + padded);
	be32(file, 0);
	be32(file, static_cast<uint32_t>(stamp >> 32));
	be32(file, static_cast<uint32_t>(stamp));
	be32(file, static_cast<uint32_t>(frame.size()));
	be32(file, static_cast<uint32_t>(frame.size()));
	file.insert(file.end(), frame.begin(), frame.end());
	file.resize(file.size() + (padded - frame.size()), 0);
	be32(file, 32 + padded);

	ReadStats stats;
	std::string error;
	const std::vector<Packet> packets = read_all(file, stats, error);
	CHECK(error.empty());
	REQUIRE(packets.size() == 1);
	CHECK(packets[0].time == doctest::Approx(1500000100.0));
	CHECK(packets[0].source_ip == "10.0.0.5");
	CHECK(packets[0].destination_ip == "239.1.2.3");
	CHECK(packets[0].data.size() == 144);
}

TEST_CASE("classic pcap: byte orders, resolutions and link types") {
	const std::vector<uint8_t> payload = pdu(1, 144);
	std::vector<uint8_t> loopback = { 2, 0, 0, 0 }; // Npcap loopback: AF_INET in host (little-endian) order
	const std::vector<uint8_t> ip = ipv4_udp(payload);
	loopback.insert(loopback.end(), ip.begin(), ip.end());
	std::vector<uint8_t> sll(16, 0);
	sll[14] = 0x08;
	sll.insert(sll.end(), ip.begin(), ip.end());
	std::vector<uint8_t> sll2(20, 0);
	sll2[0] = 0x08;
	sll2.insert(sll2.end(), ip.begin(), ip.end());

	struct Case {
		uint32_t link_type;
		std::vector<uint8_t> frame;
	};
	const Case cases[] = {
		{ 1, ethernet(ip) },
		{ 1, ethernet(ip, 0x0800, true) },
		{ 1, ethernet(ipv6_udp(payload), 0x86DD) },
		{ 0, loopback },
		{ 101, ip },
		{ 228, ip },
		{ 113, sll },
		{ 276, sll2 },
	};
	for (const Case &c : cases) {
		for (int variant = 0; variant < 4; variant++) {
			const bool big = variant & 1;
			const bool nano = variant & 2;
			CAPTURE(c.link_type);
			CAPTURE(variant);
			const std::vector<uint8_t> file = pcap_file(c.link_type, { c.frame, c.frame }, big, nano);
			CHECK(detect(file.data(), file.size()) == Format::PCAP);
			ReadStats stats;
			std::string error;
			const std::vector<Packet> packets = read_all(file, stats, error);
			CHECK(error.empty());
			REQUIRE(packets.size() == 2);
			CHECK(packets[0].data == payload);
			CHECK(packets[1].time == doctest::Approx(1700000001.25));
		}
	}
}

TEST_CASE("reading skips what is not DIS over UDP") {
	std::vector<uint8_t> tcp = ipv4_udp(pdu(1, 144));
	tcp[9] = 6;
	std::vector<uint8_t> text_payload(40, 'x');
	const std::vector<std::vector<uint8_t>> frames = {
		ethernet(ipv4_udp(pdu(1, 144))),
		ethernet(tcp),
		ethernet(ipv4_udp(pdu(1, 144), 3000, 3000, 0x2000)), // more fragments follow
		ethernet(ipv4_udp(text_payload)),
		ethernet(ipv4_udp(pdu(2, 96), 5000, 5001)),
		ethernet(ipv4_udp(pdu(1, 144)), 0x0806), // ARP
	};
	const std::vector<uint8_t> file = pcap_file(1, frames, false, false);
	ReadStats stats;
	std::string error;
	std::vector<Packet> packets = read_all(file, stats, error);
	CHECK(packets.size() == 2);
	CHECK(stats.frames == 6);
	CHECK(stats.not_udp == 2);
	CHECK(stats.fragments == 1);
	CHECK(stats.not_dis == 1);

	ReadStats filtered;
	packets = read_all(file, filtered, error, 3000);
	CHECK(packets.size() == 1);
	CHECK(filtered.filtered == 1);
}

TEST_CASE("pdulog round trip") {
	std::vector<Packet> written;
	sample_pcapng(written);
	written[2].source_ip = "fe80::1";
	const double start = written[0].time;
	std::string text = pdulog_header(start, "godot-dis tests");
	for (const Packet &p : written) {
		text += pdulog_line(p, start);
	}
	const std::vector<uint8_t> file(text.begin(), text.end());
	CHECK(detect(file.data(), file.size()) == Format::PDULOG);
	ReadStats stats;
	std::string error;
	const std::vector<Packet> packets = read_all(file, stats, error);
	CHECK(error.empty());
	REQUIRE(packets.size() == 3);
	for (size_t i = 0; i < packets.size(); i++) {
		CHECK(packets[i].data == written[i].data);
		CHECK(packets[i].source_ip == written[i].source_ip);
		CHECK(packets[i].destination_port == written[i].destination_port);
		CHECK(std::abs(packets[i].time - written[i].time) < 2e-6);
	}

	const std::string bad = text + "1.0 127.0.0.1:3000 127.0.0.1:3000 0g\n";
	std::vector<Packet> partial;
	ReadStats bad_stats;
	CHECK(read(reinterpret_cast<const uint8_t *>(bad.data()), bad.size(), Format::PDULOG, partial, error, bad_stats));
	CHECK(partial.size() == 3);
	CHECK(error.find("line 6") != std::string::npos);
}

TEST_CASE("truncated and damaged files do not crash") {
	std::vector<Packet> written;
	const std::vector<uint8_t> file = sample_pcapng(written);
	for (size_t cut = 0; cut < file.size(); cut++) {
		ReadStats stats;
		std::string error;
		std::vector<Packet> packets;
		read(file.data(), cut, Format::PCAPNG, packets, error, stats);
		CHECK(packets.size() <= written.size());
	}
	const std::vector<uint8_t> pcap = pcap_file(1, { ethernet(ipv4_udp(pdu(1, 144))), ethernet(ipv4_udp(pdu(2, 96))) },
			false, false);
	std::mt19937 rng(1278);
	for (int i = 0; i < 20000; i++) {
		std::vector<uint8_t> damaged = (i % 2 == 0) ? file : pcap;
		const int flips = 1 + static_cast<int>(rng() % 8);
		for (int f = 0; f < flips; f++) {
			damaged[rng() % damaged.size()] = static_cast<uint8_t>(rng());
		}
		if (rng() % 4 == 0) {
			damaged.resize(rng() % damaged.size());
		}
		ReadStats stats;
		std::string error;
		std::vector<Packet> packets;
		read(damaged.data(), damaged.size(), Format::UNKNOWN, packets, error, stats);
	}
	CHECK(true);
}

TEST_CASE("format detection by content and name") {
	CHECK(format_from_name("a/b/Capture.PCAPNG") == Format::PCAPNG);
	CHECK(format_from_name("x.pcap") == Format::PCAP);
	CHECK(format_from_name("x.pdulog") == Format::PDULOG);
	CHECK(format_from_name("x.bin") == Format::UNKNOWN);
	const uint8_t garbage[] = { 0xFF, 0xFE, 0x00, 0x01 };
	CHECK(detect(garbage, sizeof(garbage), "x.bin") == Format::UNKNOWN);
	CHECK(looks_like_dis(pdu(1, 144).data(), 144));
	CHECK_FALSE(looks_like_dis(pdu(1, 144).data(), 100)); // length field beyond the data
	uint8_t ip[4];
	CHECK(parse_ipv4("239.1.2.3", ip));
	CHECK(format_ipv4(ip) == "239.1.2.3");
	CHECK_FALSE(parse_ipv4("239.1.2.300", ip));
	CHECK_FALSE(parse_ipv4("fe80::1", ip));
}
