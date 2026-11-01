#pragma once

#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

// Something that wants to see every datagram a DisNetwork receives or sends, before any filter (DisRecorder).
// Register with DisNetwork::add_tap(); the network checks that the object still exists before calling it.
class DisPacketTap {
public:
	virtual ~DisPacketTap() = default;
	// `own`: an incoming datagram that carries this network's site / application (our own PDU looping back).
	virtual void tap_packet(const godot::PackedByteArray &bytes, const godot::String &ip, int port, bool outgoing,
			bool own) = 0;
};
