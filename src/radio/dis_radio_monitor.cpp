#include "radio/dis_radio_monitor.h"

#include <vector>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "entity/scene_lookup.h"
#include "network/dis_network.h"
#include "pdu/dis_pdu.h"

using namespace godot;

void DisRadioMonitor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("handle_pdu", "pdu"), &DisRadioMonitor::handle_pdu);
	ClassDB::bind_method(D_METHOD("update"), &DisRadioMonitor::update);
	ClassDB::bind_method(D_METHOD("get_radios"), &DisRadioMonitor::get_radios);
	ClassDB::bind_method(D_METHOD("is_transmitting", "entity_id"), &DisRadioMonitor::is_transmitting);
	ClassDB::bind_method(D_METHOD("get_transmitting_entities"), &DisRadioMonitor::get_transmitting_entities);
	ClassDB::bind_method(D_METHOD("clear"), &DisRadioMonitor::clear);

	DIS_BIND_PROPERTY(DisRadioMonitor, NODE_PATH, network_path);
	DIS_BIND_PROPERTY(DisRadioMonitor, FLOAT, signal_hold);
	DIS_BIND_PROPERTY(DisRadioMonitor, FLOAT, timeout);

	ADD_SIGNAL(MethodInfo("radio_added", PropertyInfo(Variant::VECTOR3I, "entity_id"), PropertyInfo(Variant::INT, "radio_number")));
	ADD_SIGNAL(MethodInfo("radio_removed", PropertyInfo(Variant::VECTOR3I, "entity_id"), PropertyInfo(Variant::INT, "radio_number")));
	ADD_SIGNAL(MethodInfo("transmission_started", PropertyInfo(Variant::VECTOR3I, "entity_id"),
			PropertyInfo(Variant::INT, "radio_number"), PropertyInfo(Variant::INT, "frequency")));
	ADD_SIGNAL(MethodInfo("transmission_ended", PropertyInfo(Variant::VECTOR3I, "entity_id"),
			PropertyInfo(Variant::INT, "radio_number")));
}

void DisRadioMonitor::_notification(int p_what) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	switch (p_what) {
		case NOTIFICATION_READY:
			connect_network();
			set_process(true);
			break;
		case NOTIFICATION_PROCESS:
			update();
			break;
		case NOTIFICATION_EXIT_TREE:
			disconnect_network();
			break;
	}
}

uint64_t DisRadioMonitor::key_of(const Vector3i &id) {
	return (static_cast<uint64_t>(id.x & 0xFFFF) << 32) | (static_cast<uint64_t>(id.y & 0xFFFF) << 16) |
			static_cast<uint64_t>(id.z & 0xFFFF);
}

double DisRadioMonitor::now() {
	return Time::get_singleton()->get_ticks_usec() / 1.0e6;
}

void DisRadioMonitor::set_network_path(const NodePath &value) {
	network_path = value;
	if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint()) {
		connect_network();
	}
}

void DisRadioMonitor::connect_network() {
	disconnect_network();
	DisNetwork *network = dis::find_node<DisNetwork>(this, network_path, "dis_network");
	if (network == nullptr) {
		return;
	}
	network->connect("transmitter_received", callable_mp(this, &DisRadioMonitor::on_transmitter));
	network->connect("signal_received", callable_mp(this, &DisRadioMonitor::on_signal));
	connected_network = ObjectID(network->get_instance_id());
}

void DisRadioMonitor::disconnect_network() {
	DisNetwork *network = Object::cast_to<DisNetwork>(ObjectDB::get_instance(connected_network));
	connected_network = ObjectID();
	if (network == nullptr) {
		return;
	}
	const Callable transmitter = callable_mp(this, &DisRadioMonitor::on_transmitter);
	const Callable signal = callable_mp(this, &DisRadioMonitor::on_signal);
	if (network->is_connected("transmitter_received", transmitter)) {
		network->disconnect("transmitter_received", transmitter);
	}
	if (network->is_connected("signal_received", signal)) {
		network->disconnect("signal_received", signal);
	}
}

DisRadioMonitor::Radio &DisRadioMonitor::radio_for(const Vector3i &entity_id, int radio_number, bool &is_new) {
	const Key key(key_of(entity_id), radio_number);
	auto found = radios.find(key);
	is_new = found == radios.end();
	if (is_new) {
		Radio radio;
		radio.entity_id = entity_id;
		radio.radio_number = radio_number;
		found = radios.emplace(key, radio).first;
	}
	return found->second;
}

void DisRadioMonitor::update_state(Radio &radio, double time) {
	const bool keyed = radio.transmitter.is_valid() &&
			radio.transmitter->get_transmit_state() == DisTransmitterPdu::TRANSMIT_ON_AND_TRANSMITTING;
	const bool transmitting = keyed || time - radio.last_signal < signal_hold;
	if (transmitting == radio.transmitting) {
		return;
	}
	radio.transmitting = transmitting;
	if (transmitting) {
		const int64_t frequency = radio.transmitter.is_valid() ? radio.transmitter->get_frequency() : 0;
		emit_signal("transmission_started", radio.entity_id, radio.radio_number, frequency);
	} else {
		emit_signal("transmission_ended", radio.entity_id, radio.radio_number);
	}
}

void DisRadioMonitor::on_transmitter(const Ref<DisTransmitterPdu> &pdu) {
	if (pdu.is_null()) {
		return;
	}
	const double time = now();
	bool is_new = false;
	Radio &radio = radio_for(pdu->get_radio_reference_id(), pdu->get_radio_number(), is_new);
	radio.transmitter = pdu;
	radio.last_heard = time;
	if (is_new) {
		emit_signal("radio_added", radio.entity_id, radio.radio_number);
	}
	update_state(radio, time);
}

void DisRadioMonitor::on_signal(const Ref<DisSignalPdu> &pdu) {
	if (pdu.is_null()) {
		return;
	}
	const double time = now();
	bool is_new = false;
	Radio &radio = radio_for(pdu->get_radio_reference_id(), pdu->get_radio_number(), is_new);
	radio.last_heard = time;
	radio.last_signal = time;
	radio.signal_count++;
	if (is_new) {
		emit_signal("radio_added", radio.entity_id, radio.radio_number);
	}
	update_state(radio, time);
}

void DisRadioMonitor::handle_pdu(const Ref<DisPduBase> &pdu) {
	ERR_FAIL_COND(pdu.is_null());
	if (pdu->get_pdu_type() == DisPdu::PDU_TYPE_TRANSMITTER) {
		on_transmitter(pdu);
	} else if (pdu->get_pdu_type() == DisPdu::PDU_TYPE_SIGNAL) {
		on_signal(pdu);
	}
}

void DisRadioMonitor::update() {
	const double time = now();
	std::vector<std::pair<Vector3i, int>> removed;
	for (auto it = radios.begin(); it != radios.end();) {
		Radio &radio = it->second;
		if (time - radio.last_heard > timeout) {
			if (radio.transmitting) {
				emit_signal("transmission_ended", radio.entity_id, radio.radio_number);
			}
			removed.emplace_back(radio.entity_id, radio.radio_number);
			it = radios.erase(it);
			continue;
		}
		update_state(radio, time);
		++it;
	}
	for (const auto &[entity_id, radio_number] : removed) {
		emit_signal("radio_removed", entity_id, radio_number);
	}
}

Dictionary DisRadioMonitor::describe(const Radio &radio) {
	Dictionary out;
	out["entity_id"] = radio.entity_id;
	out["radio_number"] = radio.radio_number;
	out["frequency"] = radio.transmitter.is_valid() ? radio.transmitter->get_frequency() : 0;
	out["transmit_state"] = radio.transmitter.is_valid() ? radio.transmitter->get_transmit_state() : -1;
	out["power"] = radio.transmitter.is_valid() ? radio.transmitter->get_power() : 0.0;
	out["transmitting"] = radio.transmitting;
	out["signal_count"] = radio.signal_count;
	out["seconds_since_heard"] = now() - radio.last_heard;
	return out;
}

Array DisRadioMonitor::get_radios() const {
	Array out;
	for (const auto &[key, radio] : radios) {
		out.push_back(describe(radio));
	}
	return out;
}

bool DisRadioMonitor::is_transmitting(const Vector3i &entity_id) const {
	const uint64_t key = key_of(entity_id);
	for (const auto &[radio_key, radio] : radios) {
		if (radio_key.first == key && radio.transmitting) {
			return true;
		}
	}
	return false;
}

Array DisRadioMonitor::get_transmitting_entities() const {
	Array out;
	for (const auto &[key, radio] : radios) {
		if (radio.transmitting && !out.has(radio.entity_id)) {
			out.push_back(radio.entity_id);
		}
	}
	return out;
}

void DisRadioMonitor::clear() {
	radios.clear();
}
