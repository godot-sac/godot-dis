#include "dis_network.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/ip.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "pdu/dis_entity_state_pdu.h"
#include "pdu/dis_pdu.h"
#include "pdu/dis_simulation_management_pdus.h"
#include "pdu/dis_warfare_pdus.h"

using namespace godot;

void DisNetwork::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start"), &DisNetwork::start);
	ClassDB::bind_method(D_METHOD("stop"), &DisNetwork::stop);
	ClassDB::bind_method(D_METHOD("is_running"), &DisNetwork::is_running);
	ClassDB::bind_method(D_METHOD("send_pdu", "pdu"), &DisNetwork::send_pdu);
	ClassDB::bind_method(D_METHOD("send_bytes", "bytes"), &DisNetwork::send_bytes);
	ClassDB::bind_method(D_METHOD("poll"), &DisNetwork::poll);
	ClassDB::bind_method(D_METHOD("inject_packet", "bytes", "sender_ip", "sender_port", "filtered"),
			&DisNetwork::inject_packet, DEFVAL("127.0.0.1"), DEFVAL(0), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_send_address"), &DisNetwork::get_send_address);
	ClassDB::bind_method(D_METHOD("get_effective_destination_port"), &DisNetwork::effective_destination_port);
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
	DIS_PROPERTY(BOOL, share_port);
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
	ADD_SIGNAL(MethodInfo("fire_received", PropertyInfo(Variant::OBJECT, "pdu", PROPERTY_HINT_RESOURCE_TYPE, "DisFirePdu")));
	ADD_SIGNAL(MethodInfo("detonation_received", PropertyInfo(Variant::OBJECT, "pdu", PROPERTY_HINT_RESOURCE_TYPE,
														  "DisDetonationPdu")));
	ADD_SIGNAL(MethodInfo("remove_entity_received", PropertyInfo(Variant::OBJECT, "pdu", PROPERTY_HINT_RESOURCE_TYPE,
															 "DisRemoveEntityPdu")));
	ADD_SIGNAL(MethodInfo("start_resume_received", PropertyInfo(Variant::OBJECT, "pdu", PROPERTY_HINT_RESOURCE_TYPE,
															"DisStartResumePdu")));
	ADD_SIGNAL(MethodInfo("stop_freeze_received", PropertyInfo(Variant::OBJECT, "pdu", PROPERTY_HINT_RESOURCE_TYPE,
														   "DisStopFreezePdu")));
	ADD_SIGNAL(MethodInfo("acknowledge_received", PropertyInfo(Variant::OBJECT, "pdu", PROPERTY_HINT_RESOURCE_TYPE,
														   "DisAcknowledgePdu")));
	ADD_SIGNAL(MethodInfo("collision_received", PropertyInfo(Variant::OBJECT, "pdu", PROPERTY_HINT_RESOURCE_TYPE,
														 "DisCollisionPdu")));
	ADD_SIGNAL(MethodInfo("create_entity_received", PropertyInfo(Variant::OBJECT, "pdu", PROPERTY_HINT_RESOURCE_TYPE,
															 "DisCreateEntityPdu")));
	ADD_SIGNAL(MethodInfo("comment_received", PropertyInfo(Variant::OBJECT, "pdu", PROPERTY_HINT_RESOURCE_TYPE,
													   "DisCommentPdu")));
	ADD_SIGNAL(MethodInfo("set_data_received", PropertyInfo(Variant::OBJECT, "pdu", PROPERTY_HINT_RESOURCE_TYPE,
														"DisSetDataPdu")));
	ADD_SIGNAL(MethodInfo("data_received", PropertyInfo(Variant::OBJECT, "pdu", PROPERTY_HINT_RESOURCE_TYPE, "DisDataPdu")));
	ADD_SIGNAL(MethodInfo("transmitter_received", PropertyInfo(Variant::OBJECT, "pdu", PROPERTY_HINT_RESOURCE_TYPE,
														   "DisTransmitterPdu")));
	ADD_SIGNAL(MethodInfo("signal_received", PropertyInfo(Variant::OBJECT, "pdu", PROPERTY_HINT_RESOURCE_TYPE,
													  "DisSignalPdu")));
	ADD_SIGNAL(MethodInfo("packet_rejected", PropertyInfo(Variant::STRING, "reason"), PropertyInfo(Variant::STRING, "sender_ip")));

	BIND_ENUM_CONSTANT(MODE_BROADCAST);
	BIND_ENUM_CONSTANT(MODE_UNICAST);
	BIND_ENUM_CONSTANT(MODE_MULTICAST);
}

void DisNetwork::_notification(int p_what) {
	// Lets DisEntity / DisEntityManager find the network without a NodePath.
	if (p_what == NOTIFICATION_ENTER_TREE) {
		add_to_group("dis_network");
	}
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

static std::string to_std(const String &text) {
	return std::string(text.utf8().get_data());
}

// (name, IPv4 address) of interfaces with a routable IPv4 address. Adapters that only have a link-local
// 169.254.x.x address (disconnected, Bluetooth, VPN placeholders) refuse to join multicast groups.
static std::vector<std::pair<String, String>> ipv4_interfaces() {
	std::vector<std::pair<String, String>> result;
	const TypedArray<Dictionary> interfaces = IP::get_singleton()->get_local_interfaces();
	for (int64_t i = 0; i < interfaces.size(); i++) {
		const Dictionary info = interfaces[i];
		const PackedStringArray addresses = info["addresses"];
		for (const String &address : addresses) {
			if (address.is_valid_ip_address() && address.contains(".") && !address.begins_with("169.254.")) {
				result.emplace_back(info["name"], address);
				break;
			}
		}
	}
	return result;
}

Error DisNetwork::start() {
	stop();
	socket = std::make_unique<dis::net::UdpSocket>();
	if (!socket->open(static_cast<uint16_t>(port), to_std(bind_address), share_port)) {
		UtilityFunctions::push_error(vformat("DisNetwork: cannot bind UDP port %d on %s: %s", port, bind_address,
				socket->last_error().c_str()));
		socket.reset();
		return ERR_CANT_OPEN;
	}
	switch (mode) {
		case MODE_BROADCAST:
			socket->set_broadcast(true);
			send_address = to_std(destination_address);
			break;
		case MODE_UNICAST:
			send_address = to_std(destination_address);
			break;
		case MODE_MULTICAST: {
			// Every routable IPv4 interface, or the one named (or addressed) by multicast_interface.
			std::vector<std::pair<String, String>> targets;
			for (const auto &[name, address] : ipv4_interfaces()) {
				if (multicast_interface.is_empty() || multicast_interface == name || multicast_interface == address) {
					targets.emplace_back(name, address);
				}
			}
			if (targets.empty() && multicast_interface.is_valid_ip_address()) {
				targets.emplace_back(multicast_interface, multicast_interface);
			}
			for (const auto &[name, address] : targets) {
				if (socket->join_multicast(to_std(multicast_group), to_std(address))) {
					joined_interfaces.push_back(name);
					joined_addresses.push_back(to_std(address));
				}
			}
			if (joined_addresses.empty()) {
				UtilityFunctions::push_error(vformat("DisNetwork: could not join multicast group %s: %s", multicast_group,
						socket->last_error().c_str()));
				stop();
				return ERR_CANT_CONNECT;
			}
			if (!multicast_interface.is_empty()) {
				socket->set_multicast_interface(joined_addresses.front());
			}
			socket->set_multicast_loop(true); // other applications on this computer receive our PDUs too
			send_address = to_std(multicast_group);
			break;
		}
	}
	return OK;
}

void DisNetwork::stop() {
	if (socket) {
		for (const std::string &address : joined_addresses) {
			socket->leave_multicast(to_std(multicast_group), address);
		}
		socket.reset(); // closes the socket
	}
	joined_interfaces.clear();
	joined_addresses.clear();
}

bool DisNetwork::is_running() const {
	return socket && socket->is_open();
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
	if (!socket->send_to(bytes.ptr(), static_cast<size_t>(bytes.size()), send_address,
				static_cast<uint16_t>(effective_destination_port()))) {
		return ERR_CANT_CONNECT;
	}
	packets_sent++;
	if (!taps.empty()) {
		call_taps(bytes, get_send_address(), effective_destination_port(), true, false);
	}
	return OK;
}

// Entity Information (1), Warfare (2), Radio Communications (4: the radio's entity), Simulation Management (5)
// and its reliable variant (10).
bool DisNetwork::starts_with_sender_id(int protocol_family) {
	return protocol_family == 1 || protocol_family == 2 || protocol_family == 4 || protocol_family == 5 ||
			protocol_family == 10;
}

const char *DisNetwork::typed_signal_of(int pdu_type) {
	switch (pdu_type) {
		case DisPdu::PDU_TYPE_ENTITY_STATE:
			return "entity_state_received";
		case DisPdu::PDU_TYPE_FIRE:
			return "fire_received";
		case DisPdu::PDU_TYPE_DETONATION:
			return "detonation_received";
		case DisPdu::PDU_TYPE_REMOVE_ENTITY:
			return "remove_entity_received";
		case DisPdu::PDU_TYPE_START_RESUME:
			return "start_resume_received";
		case DisPdu::PDU_TYPE_STOP_FREEZE:
			return "stop_freeze_received";
		case DisPdu::PDU_TYPE_ACKNOWLEDGE:
			return "acknowledge_received";
		case DisPdu::PDU_TYPE_COLLISION:
			return "collision_received";
		case DisPdu::PDU_TYPE_CREATE_ENTITY:
			return "create_entity_received";
		case DisPdu::PDU_TYPE_COMMENT:
			return "comment_received";
		case DisPdu::PDU_TYPE_SET_DATA:
			return "set_data_received";
		case DisPdu::PDU_TYPE_DATA:
			return "data_received";
		case DisPdu::PDU_TYPE_TRANSMITTER:
			return "transmitter_received";
		case DisPdu::PDU_TYPE_SIGNAL:
			return "signal_received";
		default:
			return nullptr;
	}
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
	std::string sender_address;
	uint16_t sender_port = 0;
	// is_running() again on every packet: a signal handler may stop the network (or take it out of the tree).
	while (handled < max_packets_per_frame && is_running() &&
			socket->receive(receive_buffer, sender_address, sender_port)) {
		handled++;
		PackedByteArray bytes;
		bytes.resize(static_cast<int64_t>(receive_buffer.size()));
		if (!receive_buffer.empty()) {
			memcpy(bytes.ptrw(), receive_buffer.data(), receive_buffer.size());
		}
		const String ip = String(sender_address.c_str());
		packets_received++;
		if (!taps.empty()) {
			call_taps(bytes, ip, sender_port, false, is_own(bytes));
		}
		process_packet(bytes, ip, sender_port, true);
	}
}

bool DisNetwork::is_own(const PackedByteArray &bytes) const {
	// PDUs of these families start their body with the sender's ID: site (12-13), application (14-15).
	if (bytes.size() < 16 || !starts_with_sender_id(bytes[3])) {
		return false;
	}
	const int site = (bytes[12] << 8) | bytes[13];
	const int application = (bytes[14] << 8) | bytes[15];
	return site == site_id && application == application_id;
}

namespace {
// Does a plausible PDU header start at `offset`, with all of its length inside the datagram?
bool header_at(const PackedByteArray &bytes, int64_t offset) {
	if (bytes.size() - offset < DisPdu::HEADER_SIZE) {
		return false;
	}
	const int version = bytes[offset];
	const int length = (bytes[offset + 8] << 8) | bytes[offset + 9];
	return version >= 1 && version <= 7 && bytes[offset + 2] != 0 && length >= DisPdu::HEADER_SIZE &&
			length <= bytes.size() - offset;
}
} // namespace

void DisNetwork::process_packet(const PackedByteArray &bytes, const String &ip, int port, bool filtered) {
	if (bytes.size() < DisPdu::HEADER_SIZE) {
		reject(vformat("packet too short: %d bytes", bytes.size()), ip);
		return;
	}
	// IEEE 1278.1-2012 allows several PDUs in one datagram, each starting where the previous one's length ends
	// (some senders pad each PDU to 8 bytes). Anything after the last length that is not another PDU header is
	// left to the decoder of the last PDU, as for a single PDU.
	int64_t offset = 0;
	while (offset < bytes.size()) {
		const int length = (bytes.size() - offset >= DisPdu::HEADER_SIZE) ?
				((bytes[offset + 8] << 8) | bytes[offset + 9]) :
				0;
		int64_t next = -1;
		if (length >= DisPdu::HEADER_SIZE && offset + length < bytes.size()) {
			const int64_t aligned = offset + ((length + 7) / 8) * 8;
			if (header_at(bytes, offset + length)) {
				next = offset + length;
			} else if (aligned != offset + length && header_at(bytes, aligned)) {
				next = aligned;
			}
		}
		if (next < 0) {
			process_pdu(offset == 0 ? bytes : bytes.slice(offset), ip, port, filtered);
			return;
		}
		process_pdu(bytes.slice(offset, offset + length), ip, port, filtered);
		offset = next;
	}
}

void DisNetwork::process_pdu(const PackedByteArray &bytes, const String &ip, int port, bool filtered) {
	if (bytes.size() < DisPdu::HEADER_SIZE) {
		reject(vformat("packet too short: %d bytes", bytes.size()), ip);
		return;
	}
	if (filtered && filter_exercise && bytes[1] != exercise_id) {
		packets_ignored++;
		return;
	}
	if (filtered && ignore_own_packets && is_own(bytes)) {
		packets_ignored++;
		return;
	}
	const Ref<DisPduBase> pdu = DisPdu::decode(bytes);
	if (pdu.is_null()) {
		reject(DisPdu::get_last_error(), ip);
		return;
	}
	emit_signal("pdu_received", pdu, ip, port);
	const char *typed_signal = typed_signal_of(pdu->get_pdu_type());
	if (typed_signal != nullptr) {
		emit_signal(typed_signal, pdu);
	}
}

void DisNetwork::inject_packet(const PackedByteArray &bytes, const String &ip, int port, bool filtered) {
	packets_injected++;
	process_packet(bytes, ip, port, filtered);
}

void DisNetwork::add_tap(godot::Object *owner, DisPacketTap *tap) {
	remove_tap(owner);
	taps.emplace_back(ObjectID(owner->get_instance_id()), tap);
}

void DisNetwork::remove_tap(godot::Object *owner) {
	const ObjectID id(owner->get_instance_id());
	taps.erase(std::remove_if(taps.begin(), taps.end(), [&](const auto &entry) { return entry.first == id; }), taps.end());
}

void DisNetwork::call_taps(const PackedByteArray &bytes, const String &ip, int port, bool outgoing, bool own) {
	for (size_t i = 0; i < taps.size();) {
		if (ObjectDB::get_instance(taps[i].first) == nullptr) {
			taps.erase(taps.begin() + static_cast<std::ptrdiff_t>(i)); // the recorder was freed without unregistering
			continue;
		}
		taps[i].second->tap_packet(bytes, ip, port, outgoing, own);
		i++;
	}
}

String DisNetwork::get_send_address() const {
	return String(send_address.c_str());
}

Dictionary DisNetwork::get_statistics() const {
	Dictionary stats;
	stats["received"] = packets_received;
	stats["sent"] = packets_sent;
	stats["rejected"] = packets_rejected;
	stats["ignored"] = packets_ignored;
	stats["injected"] = packets_injected;
	stats["multicast_interfaces"] = joined_interfaces;
	return stats;
}

void DisNetwork::reset_statistics() {
	packets_received = 0;
	packets_sent = 0;
	packets_rejected = 0;
	packets_ignored = 0;
	packets_injected = 0;
}
