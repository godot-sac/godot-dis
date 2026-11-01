#include "record/capture_format.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace dis::capture {

namespace {

// pcapng block types and constants
constexpr uint32_t SECTION_HEADER = 0x0A0D0D0A;
constexpr uint32_t INTERFACE_DESCRIPTION = 0x00000001;
constexpr uint32_t SIMPLE_PACKET = 0x00000003;
constexpr uint32_t ENHANCED_PACKET = 0x00000006;
constexpr uint32_t BYTE_ORDER_MAGIC = 0x1A2B3C4D;

// Link types (tcpdump.org LINKTYPE_*)
constexpr uint32_t LINK_NULL = 0; // BSD loopback, host byte order family
constexpr uint32_t LINK_ETHERNET = 1;
constexpr uint32_t LINK_RAW = 101; // raw IPv4 or IPv6
constexpr uint32_t LINK_LOOP = 108; // OpenBSD loopback, network byte order family
constexpr uint32_t LINK_LINUX_SLL = 113;
constexpr uint32_t LINK_IPV4 = 228;
constexpr uint32_t LINK_IPV6 = 229;
constexpr uint32_t LINK_LINUX_SLL2 = 276;

constexpr uint16_t ETHERTYPE_IPV4 = 0x0800;
constexpr uint16_t ETHERTYPE_IPV6 = 0x86DD;
constexpr uint16_t ETHERTYPE_VLAN = 0x8100;
constexpr uint16_t ETHERTYPE_QINQ = 0x88A8;
constexpr uint8_t PROTOCOL_UDP = 17;

uint16_t be16(const uint8_t *p) {
	return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint32_t be32(const uint8_t *p) {
	return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) |
			p[3];
}

// Reads integers in the byte order of the current pcap / pcapng section.
struct Reader {
	const uint8_t *data;
	size_t size;
	bool swap = false; // true if the file is big-endian (we decode little-endian by default)

	uint16_t u16(size_t at) const {
		const uint16_t v = static_cast<uint16_t>(data[at] | (data[at + 1] << 8));
		return swap ? static_cast<uint16_t>((v >> 8) | (v << 8)) : v;
	}
	uint32_t u32(size_t at) const {
		const uint32_t v = static_cast<uint32_t>(data[at]) | (static_cast<uint32_t>(data[at + 1]) << 8) |
				(static_cast<uint32_t>(data[at + 2]) << 16) | (static_cast<uint32_t>(data[at + 3]) << 24);
		return swap ? ((v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24)) : v;
	}
	uint64_t u64(size_t at) const {
		// pcapng stores 64-bit option values in section byte order as one integer.
		const uint64_t a = u32(at);
		const uint64_t b = u32(at + 4);
		return swap ? (a << 32) | b : (b << 32) | a;
	}
};

void put16le(std::vector<uint8_t> &out, uint16_t v) {
	out.push_back(static_cast<uint8_t>(v));
	out.push_back(static_cast<uint8_t>(v >> 8));
}

void put32le(std::vector<uint8_t> &out, uint32_t v) {
	for (int i = 0; i < 4; i++) {
		out.push_back(static_cast<uint8_t>(v >> (8 * i)));
	}
}

void put16be(std::vector<uint8_t> &out, uint16_t v) {
	out.push_back(static_cast<uint8_t>(v >> 8));
	out.push_back(static_cast<uint8_t>(v));
}

void pad4(std::vector<uint8_t> &out) {
	while (out.size() % 4 != 0) {
		out.push_back(0);
	}
}

// Appends a pcapng option (code, value) padded to 4 bytes.
void put_option(std::vector<uint8_t> &out, uint16_t code, const void *value, size_t length) {
	put16le(out, code);
	put16le(out, static_cast<uint16_t>(length));
	const uint8_t *bytes = static_cast<const uint8_t *>(value);
	out.insert(out.end(), bytes, bytes + length);
	pad4(out);
}

// Wraps `body` as a pcapng block (type, total length, body, total length).
std::vector<uint8_t> block(uint32_t type, const std::vector<uint8_t> &body) {
	std::vector<uint8_t> out;
	const uint32_t total = static_cast<uint32_t>(12 + body.size());
	put32le(out, type);
	put32le(out, total);
	out.insert(out.end(), body.begin(), body.end());
	put32le(out, total);
	return out;
}

// Finds the UDP datagram in a link-layer frame. On success fills the packet (addresses, ports, payload).
// Returns false and bumps a statistic otherwise.
bool decode_frame(uint32_t link_type, const uint8_t *frame, size_t size, Packet &packet, ReadStats &stats) {
	size_t at = 0;
	uint16_t network = 0; // ethertype of the network layer, 0 = guess from the IP version nibble
	switch (link_type) {
		case LINK_ETHERNET: {
			if (size < 14) {
				stats.not_udp++;
				return false;
			}
			network = be16(frame + 12);
			at = 14;
			while ((network == ETHERTYPE_VLAN || network == ETHERTYPE_QINQ) && at + 4 <= size) {
				network = be16(frame + at + 2);
				at += 4;
			}
		} break;
		case LINK_NULL:
		case LINK_LOOP: {
			if (size < 4) {
				stats.not_udp++;
				return false;
			}
			// Address family: 2 = AF_INET everywhere; IPv6 values differ by OS (24, 28, 30). Either byte order.
			const uint32_t family_le = static_cast<uint32_t>(frame[0]) | (frame[1] << 8) | (frame[2] << 16) |
					(static_cast<uint32_t>(frame[3]) << 24);
			const uint32_t family = family_le > 0xFFFF ? be32(frame) : family_le;
			network = family == 2 ? ETHERTYPE_IPV4 : (family == 24 || family == 28 || family == 30 ? ETHERTYPE_IPV6 : 1);
			at = 4;
		} break;
		case LINK_RAW:
			break;
		case LINK_IPV4:
			network = ETHERTYPE_IPV4;
			break;
		case LINK_IPV6:
			network = ETHERTYPE_IPV6;
			break;
		case LINK_LINUX_SLL:
			if (size < 16) {
				stats.not_udp++;
				return false;
			}
			network = be16(frame + 14);
			at = 16;
			break;
		case LINK_LINUX_SLL2:
			if (size < 20) {
				stats.not_udp++;
				return false;
			}
			network = be16(frame);
			at = 20;
			break;
		default:
			stats.not_udp++;
			return false;
	}
	if (at >= size) {
		stats.not_udp++;
		return false;
	}
	if (network == 0) {
		const int version = frame[at] >> 4;
		network = version == 4 ? ETHERTYPE_IPV4 : (version == 6 ? ETHERTYPE_IPV6 : 1);
	}

	const uint8_t *ip = frame + at;
	const size_t ip_size = size - at;
	const uint8_t *udp = nullptr;
	size_t udp_size = 0;
	if (network == ETHERTYPE_IPV4) {
		if (ip_size < 20 || (ip[0] >> 4) != 4) {
			stats.not_udp++;
			return false;
		}
		const size_t header = (ip[0] & 0x0F) * 4u;
		const size_t total = be16(ip + 2);
		if (header < 20 || total < header || ip[9] != PROTOCOL_UDP) {
			stats.not_udp++;
			return false;
		}
		const uint16_t fragment = be16(ip + 6);
		if ((fragment & 0x2000) != 0 || (fragment & 0x1FFF) != 0) {
			stats.fragments++;
			return false;
		}
		if (total > ip_size) {
			stats.truncated++;
			return false;
		}
		packet.source_ip = format_ipv4(ip + 12);
		packet.destination_ip = format_ipv4(ip + 16);
		udp = ip + header;
		udp_size = total - header;
	} else if (network == ETHERTYPE_IPV6) {
		// Only UDP directly after the fixed header; extension headers are rare for DIS traffic.
		if (ip_size < 40 || (ip[0] >> 4) != 6 || ip[6] != PROTOCOL_UDP) {
			stats.not_udp++;
			return false;
		}
		const size_t payload = be16(ip + 4);
		if (40 + payload > ip_size) {
			stats.truncated++;
			return false;
		}
		char text[64];
		std::snprintf(text, sizeof(text), "%x:%x:%x:%x:%x:%x:%x:%x", be16(ip + 8), be16(ip + 10), be16(ip + 12),
				be16(ip + 14), be16(ip + 16), be16(ip + 18), be16(ip + 20), be16(ip + 22));
		packet.source_ip = text;
		std::snprintf(text, sizeof(text), "%x:%x:%x:%x:%x:%x:%x:%x", be16(ip + 24), be16(ip + 26), be16(ip + 28),
				be16(ip + 30), be16(ip + 32), be16(ip + 34), be16(ip + 36), be16(ip + 38));
		packet.destination_ip = text;
		udp = ip + 40;
		udp_size = payload;
	} else {
		stats.not_udp++;
		return false;
	}
	if (udp_size < 8) {
		stats.not_udp++;
		return false;
	}
	const size_t length = be16(udp + 4);
	if (length < 8 || length > udp_size) {
		stats.truncated++;
		return false;
	}
	packet.source_port = be16(udp);
	packet.destination_port = be16(udp + 2);
	packet.data.assign(udp + 8, udp + length);
	return true;
}

// Keeps a decoded datagram if it passes the port filter and looks like DIS.
void keep(Packet &packet, std::vector<Packet> &out, ReadStats &stats, int port) {
	if (port > 0 && packet.source_port != port && packet.destination_port != port) {
		stats.filtered++;
		return;
	}
	if (!looks_like_dis(packet.data.data(), packet.data.size())) {
		stats.not_dis++;
		return;
	}
	out.push_back(std::move(packet));
}

struct Interface {
	uint32_t link_type = LINK_ETHERNET;
	double units_per_second = 1e6;
	int64_t offset_seconds = 0;
};

bool read_pcapng(const uint8_t *data, size_t size, std::vector<Packet> &out, std::string &error, ReadStats &stats,
		int port) {
	Reader r{ data, size };
	std::vector<Interface> interfaces;
	size_t at = 0;
	bool any_section = false;
	while (at + 12 <= size) {
		// Block type and length are in section byte order; the SHB type reads the same both ways.
		const uint32_t type = r.u32(at);
		if (type == SECTION_HEADER) {
			if (at + 28 > size) {
				error = "truncated section header";
				break;
			}
			const uint32_t magic = static_cast<uint32_t>(data[at + 8]) | (data[at + 9] << 8) | (data[at + 10] << 16) |
					(static_cast<uint32_t>(data[at + 11]) << 24);
			if (magic == BYTE_ORDER_MAGIC) {
				r.swap = false;
			} else if (magic == 0x4D3C2B1A) {
				r.swap = true;
			} else {
				error = "bad pcapng byte-order magic";
				break;
			}
			interfaces.clear();
			any_section = true;
		} else if (!any_section) {
			error = "not a pcapng file (no section header)";
			break;
		}
		const uint32_t length = r.u32(at + 4);
		if (length < 12 || length % 4 != 0 || at + length > size) {
			if (at + length > size && length >= 12) {
				stats.truncated++;
			}
			error = length < 12 || length % 4 != 0 ? "bad block length" : "file ends inside a block (cut off?)";
			break;
		}
		const size_t body = at + 8;
		const size_t body_end = at + length - 4;
		if (type == INTERFACE_DESCRIPTION && body + 8 <= body_end) {
			Interface iface;
			iface.link_type = r.u16(body);
			// Options: code, length, value padded to 4.
			size_t option = body + 8;
			while (option + 4 <= body_end) {
				const uint16_t code = r.u16(option);
				const uint16_t option_length = r.u16(option + 2);
				const size_t value = option + 4;
				if (code == 0 || value + option_length > body_end) {
					break;
				}
				if (code == 9 && option_length >= 1) { // if_tsresol
					const uint8_t resolution = data[value];
					iface.units_per_second = (resolution & 0x80) ? std::pow(2.0, resolution & 0x7F) : std::pow(10.0, resolution);
				} else if (code == 14 && option_length >= 8) { // if_tsoffset
					iface.offset_seconds = static_cast<int64_t>(r.u64(value));
				}
				option = value + ((option_length + 3u) & ~3u);
			}
			interfaces.push_back(iface);
		} else if (type == ENHANCED_PACKET && body + 20 <= body_end) {
			stats.frames++;
			const uint32_t interface_id = r.u32(body);
			const uint64_t stamp = (static_cast<uint64_t>(r.u32(body + 4)) << 32) | r.u32(body + 8);
			const uint32_t captured = r.u32(body + 12);
			const uint32_t original = r.u32(body + 16);
			if (interface_id >= interfaces.size() || body + 20 + captured > body_end) {
				stats.not_udp++;
			} else {
				const Interface &iface = interfaces[interface_id];
				Packet packet;
				packet.time = static_cast<double>(stamp) / iface.units_per_second + static_cast<double>(iface.offset_seconds);
				if (captured < original) {
					stats.truncated++;
				} else if (decode_frame(iface.link_type, data + body + 20, captured, packet, stats)) {
					keep(packet, out, stats, port);
				}
			}
		} else if (type == SIMPLE_PACKET && body + 4 <= body_end) {
			stats.frames++;
			const uint32_t original = r.u32(body);
			const size_t captured = std::min<size_t>(original, body_end - body - 4);
			Packet packet;
			packet.time = out.empty() ? 0.0 : out.back().time; // simple packets carry no timestamp
			if (interfaces.empty() || captured < original) {
				stats.not_udp++;
			} else if (decode_frame(interfaces[0].link_type, data + body + 4, captured, packet, stats)) {
				keep(packet, out, stats, port);
			}
		}
		at += length;
	}
	if (!any_section && error.empty()) {
		error = "empty pcapng file";
	}
	return any_section;
}

bool read_pcap(const uint8_t *data, size_t size, std::vector<Packet> &out, std::string &error, ReadStats &stats,
		int port) {
	if (size < 24) {
		error = "file too short for a pcap header";
		return false;
	}
	Reader r{ data, size };
	const uint32_t magic = r.u32(0);
	double units = 1e6;
	if (magic == 0xA1B2C3D4 || magic == 0xA1B23C4D) {
		r.swap = false;
	} else if (magic == 0xD4C3B2A1 || magic == 0x4D3CB2A1) {
		r.swap = true;
	} else {
		error = "not a pcap file";
		return false;
	}
	if (r.u32(0) == 0xA1B23C4D) {
		units = 1e9; // nanosecond variant
	}
	const uint32_t link_type = r.u32(20) & 0x0FFFFFFF; // upper bits may carry FCS information
	size_t at = 24;
	while (at + 16 <= size) {
		const double seconds = r.u32(at);
		const double fraction = r.u32(at + 4);
		const uint32_t captured = r.u32(at + 8);
		const uint32_t original = r.u32(at + 12);
		at += 16;
		stats.frames++;
		if (at + captured > size) {
			stats.truncated++;
			error = "file ends inside a packet (cut off?)";
			break;
		}
		Packet packet;
		packet.time = seconds + fraction / units;
		if (captured < original) {
			stats.truncated++;
		} else if (decode_frame(link_type, data + at, captured, packet, stats)) {
			keep(packet, out, stats, port);
		}
		at += captured;
	}
	return true;
}

bool parse_endpoint(const std::string &text, std::string &ip, uint16_t &port) {
	const size_t colon = text.rfind(':');
	if (colon == std::string::npos || colon == 0) {
		return false;
	}
	ip = text.substr(0, colon);
	if (ip.size() > 2 && ip.front() == '[' && ip.back() == ']') {
		ip = ip.substr(1, ip.size() - 2); // [ipv6]:port
	}
	char *end = nullptr;
	const long value = std::strtol(text.c_str() + colon + 1, &end, 10);
	if (end == text.c_str() + colon + 1 || *end != '\0' || value < 0 || value > 65535) {
		return false;
	}
	port = static_cast<uint16_t>(value);
	return true;
}

int hex_value(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

bool read_pdulog(const uint8_t *data, size_t size, std::vector<Packet> &out, std::string &error, ReadStats &stats,
		int port) {
	const std::string text(reinterpret_cast<const char *>(data), size);
	std::istringstream lines(text);
	std::string line;
	double start = 0.0;
	int number = 0;
	while (std::getline(lines, line)) {
		number++;
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		size_t first = line.find_first_not_of(" \t");
		if (first == std::string::npos) {
			continue;
		}
		if (line[first] == '#') {
			const std::string key = "# start:";
			if (line.compare(first, key.size(), key) == 0) {
				start = std::strtod(line.c_str() + first + key.size(), nullptr);
			}
			continue;
		}
		std::istringstream fields(line);
		std::string time_text, source, destination, hex;
		if (!(fields >> time_text >> source >> destination >> hex)) {
			error = "line " + std::to_string(number) + ": expected <seconds> <source> <destination> <hex>";
			return !out.empty();
		}
		Packet packet;
		char *end = nullptr;
		packet.time = start + std::strtod(time_text.c_str(), &end);
		if (end == time_text.c_str() || !parse_endpoint(source, packet.source_ip, packet.source_port) ||
				!parse_endpoint(destination, packet.destination_ip, packet.destination_port) || hex.size() % 2 != 0) {
			error = "line " + std::to_string(number) + ": malformed";
			return !out.empty();
		}
		packet.data.resize(hex.size() / 2);
		for (size_t i = 0; i < packet.data.size(); i++) {
			const int high = hex_value(hex[2 * i]);
			const int low = hex_value(hex[2 * i + 1]);
			if (high < 0 || low < 0) {
				error = "line " + std::to_string(number) + ": bad hex digit";
				return !out.empty();
			}
			packet.data[i] = static_cast<uint8_t>((high << 4) | low);
		}
		stats.frames++;
		keep(packet, out, stats, port);
	}
	return true;
}

std::string lower(std::string text) {
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
	return text;
}

bool ends_with(const std::string &text, const std::string &suffix) {
	return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

bool parse_ipv4(const std::string &text, uint8_t out[4]) {
	uint8_t parsed[4];
	size_t at = 0;
	for (int part = 0; part < 4; part++) {
		if (part > 0) {
			if (at >= text.size() || text[at] != '.') {
				return false;
			}
			at++;
		}
		const size_t start = at;
		unsigned value = 0;
		while (at < text.size() && at - start < 3 && std::isdigit(static_cast<unsigned char>(text[at]))) {
			value = value * 10 + static_cast<unsigned>(text[at] - '0');
			at++;
		}
		if (at == start || value > 255) {
			return false;
		}
		parsed[part] = static_cast<uint8_t>(value);
	}
	if (at != text.size()) {
		return false;
	}
	std::memcpy(out, parsed, 4);
	return true;
}

std::string format_ipv4(const uint8_t ip[4]) {
	char text[16];
	std::snprintf(text, sizeof(text), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
	return text;
}

uint16_t internet_checksum(const uint8_t *data, size_t size, uint32_t sum) {
	for (size_t i = 0; i + 1 < size; i += 2) {
		sum += be16(data + i);
	}
	if (size % 2 != 0) {
		sum += static_cast<uint32_t>(data[size - 1]) << 8;
	}
	while (sum >> 16) {
		sum = (sum & 0xFFFF) + (sum >> 16);
	}
	return static_cast<uint16_t>(~sum);
}

bool looks_like_dis(const uint8_t *payload, size_t size) {
	if (size < 12) {
		return false;
	}
	const int version = payload[0];
	const int length = be16(payload + 8);
	return version >= 1 && version <= 7 && payload[2] != 0 && length >= 12 && static_cast<size_t>(length) <= size;
}

Format format_from_name(const std::string &name) {
	const std::string n = lower(name);
	if (ends_with(n, ".pcapng")) {
		return Format::PCAPNG;
	}
	if (ends_with(n, ".pcap")) {
		return Format::PCAP;
	}
	if (ends_with(n, ".pdulog")) {
		return Format::PDULOG;
	}
	return Format::UNKNOWN;
}

Format detect(const uint8_t *data, size_t size, const std::string &name) {
	if (size >= 4) {
		const uint32_t first = be32(data);
		if (first == 0x0A0D0D0A) {
			return Format::PCAPNG;
		}
		if (first == 0xA1B2C3D4 || first == 0xD4C3B2A1 || first == 0xA1B23C4D || first == 0x4D3CB2A1) {
			return Format::PCAP;
		}
	}
	if (size == 0 || data[0] == '#' || std::isdigit(data[0])) {
		return Format::PDULOG;
	}
	return format_from_name(name);
}

const char *format_name(Format format) {
	switch (format) {
		case Format::PCAPNG:
			return "pcapng";
		case Format::PCAP:
			return "pcap";
		case Format::PDULOG:
			return "pdulog";
		default:
			return "unknown";
	}
}

bool read(const uint8_t *data, size_t size, Format format, std::vector<Packet> &out, std::string &error, ReadStats &stats,
		int port) {
	if (format == Format::UNKNOWN) {
		format = detect(data, size);
	}
	switch (format) {
		case Format::PCAPNG:
			return read_pcapng(data, size, out, error, stats, port);
		case Format::PCAP:
			return read_pcap(data, size, out, error, stats, port);
		case Format::PDULOG:
			return read_pdulog(data, size, out, error, stats, port);
		default:
			error = "unknown file format";
			return false;
	}
}

std::vector<uint8_t> pcapng_file_header(const std::string &application) {
	std::vector<uint8_t> section;
	put32le(section, BYTE_ORDER_MAGIC);
	put16le(section, 1); // version 1.0
	put16le(section, 0);
	put32le(section, 0xFFFFFFFF); // section length: unknown (-1)
	put32le(section, 0xFFFFFFFF);
	put_option(section, 4, application.data(), application.size()); // shb_userappl
	put32le(section, 0); // opt_endofopt
	std::vector<uint8_t> out = block(SECTION_HEADER, section);

	std::vector<uint8_t> iface;
	put16le(iface, static_cast<uint16_t>(LINK_ETHERNET));
	put16le(iface, 0);
	put32le(iface, 0); // snap length: no limit
	const uint8_t microseconds = 6;
	put_option(iface, 9, &microseconds, 1); // if_tsresol
	put32le(iface, 0);
	const std::vector<uint8_t> interface_block = block(INTERFACE_DESCRIPTION, iface);
	out.insert(out.end(), interface_block.begin(), interface_block.end());
	return out;
}

std::vector<uint8_t> pcapng_packet_record(const Packet &packet) {
	uint8_t source[4] = { 127, 0, 0, 1 };
	uint8_t destination[4] = { 127, 0, 0, 1 };
	parse_ipv4(packet.source_ip, source);
	parse_ipv4(packet.destination_ip, destination);

	// Ethernet: multicast and broadcast destinations get their standard MAC addresses, the rest locally
	// administered placeholders (the real ones are not known).
	std::vector<uint8_t> frame;
	if (destination[0] >= 224 && destination[0] <= 239) {
		frame.insert(frame.end(), { 0x01, 0x00, 0x5E, static_cast<uint8_t>(destination[1] & 0x7F), destination[2],
										   destination[3] });
	} else if (destination[3] == 255) {
		frame.insert(frame.end(), 6, 0xFF);
	} else {
		frame.insert(frame.end(), { 0x02, 0x00, 0x00, 0x00, 0x00, 0x02 });
	}
	frame.insert(frame.end(), { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 });
	put16be(frame, ETHERTYPE_IPV4);

	const size_t udp_length = 8 + packet.data.size();
	const size_t ip_start = frame.size();
	frame.insert(frame.end(), { 0x45, 0x00 });
	put16be(frame, static_cast<uint16_t>(20 + udp_length));
	put16be(frame, 0); // identification
	put16be(frame, 0x4000); // don't fragment
	frame.insert(frame.end(), { 64, PROTOCOL_UDP, 0, 0 }); // TTL, protocol, checksum placeholder
	frame.insert(frame.end(), source, source + 4);
	frame.insert(frame.end(), destination, destination + 4);
	const uint16_t ip_checksum = internet_checksum(frame.data() + ip_start, 20);
	frame[ip_start + 10] = static_cast<uint8_t>(ip_checksum >> 8);
	frame[ip_start + 11] = static_cast<uint8_t>(ip_checksum);

	const size_t udp_start = frame.size();
	put16be(frame, packet.source_port);
	put16be(frame, packet.destination_port);
	put16be(frame, static_cast<uint16_t>(udp_length));
	put16be(frame, 0); // checksum placeholder
	frame.insert(frame.end(), packet.data.begin(), packet.data.end());
	// UDP checksum over the pseudo header (addresses, protocol, length) and the datagram.
	uint32_t pseudo = be16(source) + be16(source + 2) + be16(destination) + be16(destination + 2) + PROTOCOL_UDP +
			static_cast<uint32_t>(udp_length);
	uint16_t udp_checksum = internet_checksum(frame.data() + udp_start, udp_length, pseudo);
	if (udp_checksum == 0) {
		udp_checksum = 0xFFFF;
	}
	frame[udp_start + 6] = static_cast<uint8_t>(udp_checksum >> 8);
	frame[udp_start + 7] = static_cast<uint8_t>(udp_checksum);

	std::vector<uint8_t> body;
	const double time = std::max(0.0, packet.time);
	const uint64_t stamp = static_cast<uint64_t>(std::llround(time * 1e6));
	put32le(body, 0); // interface 0
	put32le(body, static_cast<uint32_t>(stamp >> 32));
	put32le(body, static_cast<uint32_t>(stamp));
	put32le(body, static_cast<uint32_t>(frame.size()));
	put32le(body, static_cast<uint32_t>(frame.size()));
	body.insert(body.end(), frame.begin(), frame.end());
	pad4(body);
	return block(ENHANCED_PACKET, body);
}

std::string pdulog_header(double start, const std::string &application) {
	char text[128];
	std::snprintf(text, sizeof(text), "# start: %.6f\n", start);
	return "# DIS PDU log 1 (" + application + "): <seconds since start> <source> <destination> <PDU hex>\n" + text;
}

std::string pdulog_line(const Packet &packet, double start) {
	static const char *digits = "0123456789abcdef";
	char prefix[160];
	const bool source_v6 = packet.source_ip.find(':') != std::string::npos;
	const bool destination_v6 = packet.destination_ip.find(':') != std::string::npos;
	std::snprintf(prefix, sizeof(prefix), "%.6f %s%s%s:%u %s%s%s:%u ", packet.time - start, source_v6 ? "[" : "",
			packet.source_ip.c_str(), source_v6 ? "]" : "", packet.source_port, destination_v6 ? "[" : "",
			packet.destination_ip.c_str(), destination_v6 ? "]" : "", packet.destination_port);
	std::string line = prefix;
	line.reserve(line.size() + packet.data.size() * 2 + 1);
	for (const uint8_t byte : packet.data) {
		line.push_back(digits[byte >> 4]);
		line.push_back(digits[byte & 0x0F]);
	}
	line.push_back('\n');
	return line;
}

} // namespace dis::capture
