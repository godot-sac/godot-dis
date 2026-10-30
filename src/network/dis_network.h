#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/packet_peer_udp.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

// Sends and receives DIS PDUs over UDP. Add one to the scene, set the mode and port, and connect to
// `pdu_received` / `entity_state_received`. Receiving is non-blocking: every frame (_process) all
// waiting packets are read, decoded and emitted.
//
// Own packets are recognised by the simulation address (site, application) of the sending entity,
// so broadcasts that loop back to this machine are ignored when `ignore_own_packets` is on.
class DisNetwork : public godot::Node {
	GDCLASS(DisNetwork, godot::Node)

public:
	enum Mode {
		MODE_BROADCAST, // send to destination_address (default 255.255.255.255), receive on port
		MODE_UNICAST, // send to one host
		MODE_MULTICAST, // join multicast_group, send to it
	};

private:
	Mode mode = MODE_BROADCAST;
	int port = 3000;
	godot::String bind_address = "*";
	godot::String destination_address = "255.255.255.255";
	int destination_port = 0; // 0 = same as port
	godot::String multicast_group = "239.1.2.3";
	godot::String multicast_interface; // empty = every interface
	int exercise_id = 1;
	bool filter_exercise = true;
	int site_id = 1;
	int application_id = 1;
	bool ignore_own_packets = true;
	bool autostart = true;
	int max_packets_per_frame = 1000;

	godot::Ref<godot::PacketPeerUDP> socket;
	godot::PackedStringArray joined_interfaces;
	int64_t packets_received = 0;
	int64_t packets_sent = 0;
	int64_t packets_rejected = 0;
	int64_t packets_ignored = 0;

	void receive_waiting_packets();
	void reject(const godot::String &reason, const godot::String &ip);
	int effective_destination_port() const;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	// Opens the socket. Called automatically on _ready when autostart is on.
	godot::Error start();
	void stop();
	bool is_running() const;

	// Encodes a PDU object (anything with encode() -> PackedByteArray, e.g. DisEntityStatePdu) and sends it.
	// Sets its exercise_id to this network's exercise_id first.
	godot::Error send_pdu(const godot::Ref<godot::RefCounted> &pdu);
	// Sends raw bytes to the configured destination.
	godot::Error send_bytes(const godot::PackedByteArray &bytes);
	// Reads and dispatches waiting packets now (normally done every frame).
	void poll();

	godot::Dictionary get_statistics() const;
	void reset_statistics();

	void set_mode(Mode value) { mode = value; }
	Mode get_mode() const { return mode; }
	void set_port(int value) { port = value; }
	int get_port() const { return port; }
	void set_bind_address(const godot::String &value) { bind_address = value; }
	godot::String get_bind_address() const { return bind_address; }
	void set_destination_address(const godot::String &value) { destination_address = value; }
	godot::String get_destination_address() const { return destination_address; }
	void set_destination_port(int value) { destination_port = value; }
	int get_destination_port() const { return destination_port; }
	void set_multicast_group(const godot::String &value) { multicast_group = value; }
	godot::String get_multicast_group() const { return multicast_group; }
	void set_multicast_interface(const godot::String &value) { multicast_interface = value; }
	godot::String get_multicast_interface() const { return multicast_interface; }
	void set_exercise_id(int value) { exercise_id = value; }
	int get_exercise_id() const { return exercise_id; }
	void set_filter_exercise(bool value) { filter_exercise = value; }
	bool get_filter_exercise() const { return filter_exercise; }
	void set_site_id(int value) { site_id = value; }
	int get_site_id() const { return site_id; }
	void set_application_id(int value) { application_id = value; }
	int get_application_id() const { return application_id; }
	void set_ignore_own_packets(bool value) { ignore_own_packets = value; }
	bool get_ignore_own_packets() const { return ignore_own_packets; }
	void set_autostart(bool value) { autostart = value; }
	bool get_autostart() const { return autostart; }
	void set_max_packets_per_frame(int value) { max_packets_per_frame = value; }
	int get_max_packets_per_frame() const { return max_packets_per_frame; }
};

VARIANT_ENUM_CAST(DisNetwork::Mode);
