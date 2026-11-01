#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Reading and writing DIS recordings, without Godot (so the C++ unit tests can cover it). File formats:
// docs/recording.md.
//
//  - pcapng (and classic pcap, read only): what Wireshark, tshark and other network tools read and write. We write
//    one Ethernet interface and synthesize Ethernet + IPv4 + UDP headers around each PDU. We read Ethernet (with
//    VLAN tags), BSD loopback (Npcap's loopback adapter), raw IPv4 / IPv6 and Linux cooked (SLL, SLL2) captures and
//    keep the UDP payloads that look like DIS PDUs.
//  - pdulog: our own text format, one datagram per line: "<seconds> <source ip:port> <destination ip:port> <hex>",
//    with "#" comment lines; "# start: <UNIX seconds>" gives the time the seconds count from.
namespace dis::capture {

struct Packet {
	double time = 0.0; // UNIX seconds
	std::vector<uint8_t> data; // the UDP payload: one DIS PDU (or several, bundled)
	std::string source_ip = "127.0.0.1";
	uint16_t source_port = 3000;
	std::string destination_ip = "127.0.0.1";
	uint16_t destination_port = 3000;
};

enum class Format {
	UNKNOWN,
	PCAPNG,
	PCAP,
	PDULOG,
};

// What a read skipped; frames counts every packet record in the file.
struct ReadStats {
	int64_t frames = 0;
	int64_t not_udp = 0; // other protocols, unknown link types, IPv6 extension headers
	int64_t fragments = 0; // IPv4 fragments (DIS PDUs fit in one datagram, fragments are skipped)
	int64_t not_dis = 0; // UDP payloads that don't start with a plausible DIS header
	int64_t filtered = 0; // other UDP ports (when a port filter is given)
	int64_t truncated = 0; // packets cut short by the capture's snap length, or a cut-off last record
};

// Guesses the format from the first bytes; `name` (a file name) breaks ties for text files.
Format detect(const uint8_t *data, size_t size, const std::string &name = "");
Format format_from_name(const std::string &name);
const char *format_name(Format format);

// True if `payload` starts with something that looks like a DIS PDU header (version 1-7, sane length).
bool looks_like_dis(const uint8_t *payload, size_t size);

// Reads a whole file image. Packets are appended to `out` in file order. `port` > 0 keeps only UDP datagrams
// to or from that port. Returns false (with `error` set) only if nothing could be read at all; damage later in the
// file stops the read and is reported in `error` with the packets so far kept.
bool read(const uint8_t *data, size_t size, Format format, std::vector<Packet> &out, std::string &error,
		ReadStats &stats, int port = 0);

// pcapng writer pieces: a file is file_header() followed by packet_record() per packet.
std::vector<uint8_t> pcapng_file_header(const std::string &application);
std::vector<uint8_t> pcapng_packet_record(const Packet &packet);

// pdulog writer pieces: pdulog_header() then pdulog_line() per packet (times relative to `start`).
std::string pdulog_header(double start, const std::string &application);
std::string pdulog_line(const Packet &packet, double start);

// Helpers, exposed for tests.
bool parse_ipv4(const std::string &text, uint8_t out[4]);
std::string format_ipv4(const uint8_t ip[4]);
uint16_t internet_checksum(const uint8_t *data, size_t size, uint32_t initial = 0);

} // namespace dis::capture
