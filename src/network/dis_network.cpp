#include "dis_network.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/ip.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "pdu/dis_entity_state_pdu.h"
#include "pdu/dis_pdu.h"

using namespace godot;

void DisNetwork::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start"), &DisNetwork::start);
	ClassDB::bind_method(D_METHOD("stop"), &DisNetwork::stop);
	ClassDB::bind_method(D_METHOD("is_running"), &DisNetwork::is_running);
	ClassDB::bind_method(D_METHOD("send_pdu", "pdu"), &DisNetwork::send_pdu);
	ClassDB::bind_method(D_METHOD("send_bytes", "bytes"), &DisNetwork::send_bytes);
	ClassDB::bind_method(D_METHOD("poll"), &DisNetwork::poll);
	ClassDB::bind_method(D_METHOD("get_statistics"), &DisNetwork::get_statistics);
	ClassDB::bind_method(D_METHOD("reset_statistics"), &DisNetwork::reset_statistics);

// Binds get_<name>/set_<name> and registers the property. DIS_PROPERTY_HINT adds an editor hint.
// (Two macros because MSVC's traditional preprocessor can't drop the comma before an empty __VA_ARGS__.)
#define DIS_PROPERTY_HINT(variant_type, name, hint, hint_string)                           \
	ClassDB::bind_method(D_METHOD("set_" #name, "value"), &DisNetwork::set_##name);      \
	ClassDB::bind_method(D_METHOD("get_" #name), &DisNetwork::get_##name);               \
	ADD_PROPERTY(PropertyInfo(Variant::variant_type, #name, hint, hint_string), "set_" #name, "get_" #name)
#define DIS_PROPERTY(variant_type, name) DIS_PROPERTY_HINT(variant_type, name, PROPERTY_HINT_NONE, "")

	ADD_GROUP("Connection", "");
	DIS_PROPERTY_HINT(INT, mode, PROPERTY_HINT_ENUM, "Broadcast,Unicast,Multicast");
	DIS_PROPERTY_HINT(INT, port, PROPERTY_HINT_RANGE, "1,65535");
	DIS_PROPERTY(STRING, bind_address);
	DIS_PROPERTY(STRING, destination_address);
	DIS_PROPERTY_HINT(INT, destination_port, PROPERTY_HINT_RANGE, "0,65535");
	DIS_PROPERTY(STRING, multicast_group);
	DIS_PROPERTY(STRING, multicast_interface);
	DIS_PROPERTY(BOOL, autostart);
	ADD_GROUP("Simulation", "");
	DIS_PROPERTY_HINT(INT, exercise_id, PROPERTY_HINT_RANGE, "0,255");
	DIS_PROPERTY(BOOL, filter_exercise);
	DIS_PROPERTY_HINT(INT, site_id, PROPERTY_HINT_RANGE, "0,65535");
	DIS_PROPERTY_HINT(INT, application_id, PROPERTY_HINT_RANGE, "0,65535");
	DIS_PROPERTY(BOOL, ignore_own_packets);
	DIS_PROPERTY_HINT(INT, max_packets_per_frame, PROPERTY_HINT_RANGE, "1,100000");
#undef DIS_PROPERTY
#undef DIS_PROPERTY_HINT

	ADD_SIGNAL(MethodInfo("pdu_received", PropertyInfo(Variant::OBJECT, "pdu"), PropertyInfo(Variant::STRING, "sender_ip"),
			PropertyInfo(Variant::INT, "sender_port")));
	ADD_SIGNAL(MethodInfo("entity_state_received", PropertyInfo(Variant::OBJECT, "pdu", PROPERTY_HINT_RESOURCE_TYPE,
															   "DisEntityStatePdu")));
	ADD_SIGNAL(MethodInfo("packet_rejected", PropertyInfo(Variant::STRING, "reason"), PropertyInfo(Variant::STRING, "sender_ip")));

	BIND_ENUM_CONSTANT(MODE_BROADCAST);
	BIND_ENUM_CONSTANT(MODE_UNICAST);
	BIND_ENUM_CONSTANT(MODE_MULTICAST);
}

void DisNetwork::_notification(int p_what) {
	// Don't open sockets inside the editor.
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	switch (p_what) {
		case NOTIFICATION_READY:
			set_process(true);
			if (autostart) {
				start();
			}
			break;
		case NOTIFICATION_PROCESS:
			poll();
			break;
		case NOTIFICATION_EXIT_TREE:
			stop();
			break;
	}
}

int DisNetwork::effective_destination_port() const {
	return destination_port > 0 ? destination_port : port;
}

Error DisNetwork::start() {
	stop();
	socket.instantiate();
	Error err = socket->bind(port, bind_address);
	if (err != OK) {
		UtilityFunctions::push_error(vformat("DisNetwork: cannot bind UDP port %d on %s (error %d)", port, bind_address, err));
		socket.unref();
		return err;
	}
	switch (mode) {
		case MODE_BROADCAST:
			socket->set_broadcast_enabled(true);
			socket->set_dest_address(destination_address, effective_destination_port());
			break;
		case MODE_UNICAST:
			socket->set_dest_address(destination_address, effective_destination_port());
			break;
		case MODE_MULTICAST: {
			PackedStringArray names;
			if (multicast_interface.is_empty()) {
				// Every interface with a routable IPv4 address. Adapters that only have a link-local
				// 169.254.x.x address (disconnected, Bluetooth, VPN placeholders) refuse to join.
				const TypedArray<Dictionary> interfaces = IP::get_singleton()->get_local_interfaces();
				for (int64_t i = 0; i < interfaces.size(); i++) {
					const Dictionary info = interfaces[i];
					const PackedStringArray addresses = info["addresses"];
					for (const String &address : addresses) {
						if (address.contains(".") && !address.begins_with("169.254.")) {
							names.push_back(info["name"]);
							break;
						}
					}
				}
			} else {
				names.push_back(multicast_interface);
			}
			for (const String &name : names) {
				if (socket->join_multicast_group(multicast_group, name) == OK) {
					joined_interfaces.push_back(name);
				}
			}
			if (joined_interfaces.is_empty()) {
				UtilityFunctions::push_error(vformat("DisNetwork: could not join multicast group %s", multicast_group));
				stop();
				return ERR_CANT_CONNECT;
			}
			socket->set_dest_address(multicast_group, effective_destination_port());
			break;
		}
	}
	return OK;
}

void DisNetwork::stop() {
	if (socket.is_valid()) {
		for (const String &name : joined_interfaces) {
			socket->leave_multicast_group(multicast_group, name);
		}
		socket->close();
		socket.unref();
	}
	joined_interfaces.clear();
}

bool DisNetwork::is_running() const {
	return socket.is_valid() && socket->is_bound();
}

Error DisNetwork::send_pdu(const Ref<RefCounted> &pdu) {
	ERR_FAIL_COND_V_MSG(pdu.is_null(), ERR_INVALID_PARAMETER, "DisNetwork.send_pdu: pdu is null");
	ERR_FAIL_COND_V_MSG(!pdu->has_method("encode"), ERR_INVALID_PARAMETER, "DisNetwork.send_pdu: object has no encode()");
	if (pdu->get("exercise_id").get_type() == Variant::INT) {
		pdu->set("exercise_id", exercise_id);
	}
	const PackedByteArray bytes = pdu->call("encode");
	return send_bytes(bytes);
}

Error DisNetwork::send_bytes(const PackedByteArray &bytes) {
	ERR_FAIL_COND_V_MSG(!is_running(), ERR_UNCONFIGURED, "DisNetwork is not running; call start() first");
	const Error err = socket->put_packet(bytes);
	if (err == OK) {
		packets_sent++;
	}
	return err;
}

void DisNetwork::reject(const String &reason, const String &ip) {
	packets_rejected++;
	emit_signal("packet_rejected", reason, ip);
}

void DisNetwork::poll() {
	if (!is_running()) {
		return;
	}
	int handled = 0;
	while (socket->get_available_packet_count() > 0 && handled < max_packets_per_frame) {
		handled++;
		const PackedByteArray bytes = socket->get_packet();
		const String ip = socket->get_packet_ip();
		const int sender_port = socket->get_packet_port();
		packets_received++;

		if (bytes.size() < DisPdu::HEADER_SIZE) {
			reject(vformat("packet too short: %d bytes", bytes.size()), ip);
			continue;
		}
		if (filter_exercise && bytes[1] != exercise_id) {
			packets_ignored++;
			continue;
		}
		// Entity Information family PDUs start their body with the entity ID: site (12-13), application (14-15).
		if (ignore_own_packets && bytes.size() >= 16 && bytes[2] == DisPdu::PDU_TYPE_ENTITY_STATE) {
			const int site = (bytes[12] << 8) | bytes[13];
			const int application = (bytes[14] << 8) | bytes[15];
			if (site == site_id && application == application_id) {
				packets_ignored++;
				continue;
			}
		}
		const Ref<RefCounted> pdu = DisPdu::decode(bytes);
		if (pdu.is_null()) {
			reject(DisPdu::get_last_error(), ip);
			continue;
		}
		emit_signal("pdu_received", pdu, ip, sender_port);
		const Ref<DisEntityStatePdu> entity_state = pdu;
		if (entity_state.is_valid()) {
			emit_signal("entity_state_received", entity_state);
		}
	}
}

Dictionary DisNetwork::get_statistics() const {
	Dictionary stats;
	stats["received"] = packets_received;
	stats["sent"] = packets_sent;
	stats["rejected"] = packets_rejected;
	stats["ignored"] = packets_ignored;
	stats["multicast_interfaces"] = joined_interfaces;
	return stats;
}

void DisNetwork::reset_statistics() {
	packets_received = 0;
	packets_sent = 0;
	packets_rejected = 0;
	packets_ignored = 0;
}
