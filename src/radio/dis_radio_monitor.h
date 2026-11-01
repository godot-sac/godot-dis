#pragma once

#include <cstdint>
#include <map>
#include <utility>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include "pdu/dis_radio_pdus.h"

class DisNetwork;

// Keeps track of the radios on the network from Transmitter and Signal PDUs: which entity has which radio on which
// frequency, and who is transmitting right now. No audio is decoded or played.
//
// A radio is "transmitting" while its last Transmitter PDU says "on and transmitting" or a Signal PDU from it
// arrived less than signal_hold seconds ago. Radios not heard from for `timeout` seconds (no Transmitter or
// Signal PDU) are forgotten.
class DisRadioMonitor : public godot::Node {
	GDCLASS(DisRadioMonitor, godot::Node)

	struct Radio {
		godot::Vector3i entity_id;
		int radio_number = 0;
		godot::Ref<DisTransmitterPdu> transmitter; // last Transmitter PDU, may be null (only Signals seen)
		double last_heard = 0.0;
		double last_signal = -1.0e9;
		int64_t signal_count = 0;
		bool transmitting = false;
	};

	godot::NodePath network_path;
	double signal_hold = 0.5;
	double timeout = 12.0;

	using Key = std::pair<uint64_t, int>; // entity, radio number
	std::map<Key, Radio> radios;
	godot::ObjectID connected_network;

	static uint64_t key_of(const godot::Vector3i &id);
	static double now();
	Radio &radio_for(const godot::Vector3i &entity_id, int radio_number, bool &is_new);
	void update_state(Radio &radio, double time);
	void on_transmitter(const godot::Ref<DisTransmitterPdu> &pdu);
	void on_signal(const godot::Ref<DisSignalPdu> &pdu);
	void connect_network();
	void disconnect_network();
	static godot::Dictionary describe(const Radio &radio);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	// Applies a Transmitter or Signal PDU (normally connected to the DisNetwork signals).
	void handle_pdu(const godot::Ref<DisPduBase> &pdu);
	// Updates transmitting states and forgets silent radios; called every frame.
	void update();

	// [{entity_id, radio_number, frequency, transmit_state, transmitting, power, signal_count, seconds_since_heard}]
	godot::Array get_radios() const;
	bool is_transmitting(const godot::Vector3i &entity_id) const;
	// Entity IDs with at least one radio transmitting now.
	godot::Array get_transmitting_entities() const;
	void clear();

	void set_network_path(const godot::NodePath &value);
	godot::NodePath get_network_path() const { return network_path; }
	void set_signal_hold(double value) { signal_hold = value; }
	double get_signal_hold() const { return signal_hold; }
	void set_timeout(double value) { timeout = value; }
	double get_timeout() const { return timeout; }
};
