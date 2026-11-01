#pragma once

#include <vector>

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "record/capture_format.h"

// A DIS recording in memory: datagrams with their UNIX time and addresses, in time order. Loads and saves
// pcapng, pcap (read only) and .pdulog files (see docs/recording.md); DisPlayer plays it back.
class DisRecording : public godot::RefCounted {
	GDCLASS(DisRecording, godot::RefCounted)

	std::vector<dis::capture::Packet> packets;
	dis::capture::ReadStats stats;
	godot::String last_error;

protected:
	static void _bind_methods();

public:
	enum Format {
		FORMAT_AUTO, // from the file name, pcapng if it has no known extension
		FORMAT_PCAPNG,
		FORMAT_PDULOG,
	};

	// Replaces the contents with a file (format from its first bytes). `port` > 0 keeps only that UDP port.
	// Returns OK if anything was read; details in get_last_error() and get_read_statistics().
	godot::Error load(const godot::String &path, int port);
	godot::Error save(const godot::String &path, Format format) const;
	void clear();
	// Appends a datagram; unix_time < 0 means now. Keeps the time order.
	void add_packet(const godot::PackedByteArray &bytes, double unix_time, const godot::String &source_ip,
			int source_port, const godot::String &destination_ip, int destination_port);

	int get_packet_count() const { return static_cast<int>(packets.size()); }
	// UNIX time of the first packet (0 if empty) and the span to the last one.
	double get_start_time() const;
	double get_duration() const;
	// Seconds since the first packet.
	double get_time(int index) const;
	godot::PackedByteArray get_bytes(int index) const;
	// {time, unix_time, bytes, source_ip, source_port, destination_ip, destination_port}
	godot::Dictionary get_packet(int index) const;
	// Index of the first packet at or after `time` (seconds since the first packet); get_packet_count() if none.
	int find_index(double time) const;
	// {packets, duration, start_time, pdu_types: {type: count}, exercises: [...], entities, sites: ["site:app"]}
	godot::Dictionary get_summary() const;
	godot::String get_last_error() const { return last_error; }
	// What the last load() skipped: {frames, not_udp, fragments, not_dis, filtered, truncated}.
	godot::Dictionary get_read_statistics() const;

	// For DisPlayer.
	const std::vector<dis::capture::Packet> &get_packets() const { return packets; }
};

VARIANT_ENUM_CAST(DisRecording::Format);
