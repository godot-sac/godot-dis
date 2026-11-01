#pragma once

#include <string>
#include <vector>

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/string.hpp>

#include "network/packet_tap.h"
#include "record/dis_recording.h"

class DisNetwork;

// Records every datagram a DisNetwork receives (before its filters, so other exercises too) and, with
// record_sent, every one it sends, into a pcapng or .pdulog file (format from the file name). The file is written
// as it goes and flushed every flush_interval seconds, so a crash loses at most that much.
// Own PDUs that loop back (broadcast / multicast) are recorded once: as sent with record_sent, as received without.
class DisRecorder : public godot::Node, public DisPacketTap {
	GDCLASS(DisRecorder, godot::Node)

	godot::NodePath network_path;
	godot::String path = "user://recording.pcapng";
	int format = DisRecording::FORMAT_AUTO;
	bool record_sent = true;
	bool autostart = false;
	double flush_interval = 1.0;

	godot::Ref<godot::FileAccess> file;
	godot::String recording_path;
	bool pdulog = false;
	double start_time = 0.0;
	double last_flush = 0.0;
	int64_t packet_count = 0;
	int64_t byte_count = 0;
	std::vector<uint8_t> buffer;
	godot::ObjectID network_id;
	std::string local_ip = "127.0.0.1";
	std::string receive_destination = "127.0.0.1"; // our side of received datagrams
	int network_port = 3000;

	DisNetwork *get_network();
	void flush();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	// Starts writing to `file_path` (or `path` if empty). Stops a running recording first.
	godot::Error start(const godot::String &file_path);
	void stop();
	bool is_recording() const { return file.is_valid(); }
	int64_t get_packet_count() const { return packet_count; }
	// Seconds since start().
	double get_elapsed() const;
	godot::String get_recording_path() const { return recording_path; }

	void tap_packet(const godot::PackedByteArray &bytes, const godot::String &ip, int port, bool outgoing,
			bool own) override;

	void set_network_path(const godot::NodePath &value) { network_path = value; }
	godot::NodePath get_network_path() const { return network_path; }
	void set_path(const godot::String &value) { path = value; }
	godot::String get_path() const { return path; }
	void set_format(int value) { format = value; }
	int get_format() const { return format; }
	void set_record_sent(bool value) { record_sent = value; }
	bool get_record_sent() const { return record_sent; }
	void set_autostart(bool value) { autostart = value; }
	bool get_autostart() const { return autostart; }
	void set_flush_interval(double value) { flush_interval = value; }
	double get_flush_interval() const { return flush_interval; }
};
