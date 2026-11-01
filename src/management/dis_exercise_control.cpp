#include "dis_exercise_control.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "entity/dis_entity.h"
#include "entity/scene_lookup.h"
#include "network/dis_network.h"
#include "pdu/dis_pdu.h"

using namespace godot;

namespace {
constexpr int ALL = DisSimulationManagementPdu::ID_ALL;
constexpr size_t HANDLED_MEMORY = 64;
// A requested start/stop time further away than this is taken as a clock mismatch and applied at once.
constexpr double MAX_SCHEDULE_AHEAD = 24.0 * 3600.0;

uint64_t request_key(const Ref<DisSimulationManagementPdu> &pdu) {
	const Vector3i from = pdu->get_originating_id();
	return (static_cast<uint64_t>(from.x & 0xFFFF) << 48) | (static_cast<uint64_t>(from.y & 0xFFFF) << 40) |
			(static_cast<uint64_t>(pdu->get_pdu_type() & 0xFF) << 32) | static_cast<uint64_t>(pdu->get_request_id() & 0xFFFFFFFF);
}
} // namespace

DisExerciseControl::DisExerciseControl() {}

void DisExerciseControl::_bind_methods() {
	ClassDB::bind_method(D_METHOD("send_start_resume", "receiving_id", "real_world_time", "simulation_time"),
			&DisExerciseControl::send_start_resume, DEFVAL(Vector3i(ALL, ALL, ALL)), DEFVAL(0.0), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("send_stop_freeze", "receiving_id", "reason", "frozen_behavior", "real_world_time"),
			&DisExerciseControl::send_stop_freeze, DEFVAL(Vector3i(ALL, ALL, ALL)),
			DEFVAL(DisStopFreezePdu::REASON_RECESS), DEFVAL(0), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("send_remove_entity", "entity_id"), &DisExerciseControl::send_remove_entity);
	ClassDB::bind_method(D_METHOD("send_create_entity", "receiving_id"), &DisExerciseControl::send_create_entity);
	ClassDB::bind_method(D_METHOD("get_pending_requests"), &DisExerciseControl::get_pending_requests);
	ClassDB::bind_method(D_METHOD("start"), &DisExerciseControl::start);
	ClassDB::bind_method(D_METHOD("stop", "frozen_behavior"), &DisExerciseControl::stop, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("handle_pdu", "pdu"), &DisExerciseControl::handle_pdu);
	ClassDB::bind_method(D_METHOD("get_state"), &DisExerciseControl::get_state);
	ClassDB::bind_method(D_METHOD("is_running"), &DisExerciseControl::is_running);
	ClassDB::bind_method(D_METHOD("get_frozen_behavior"), &DisExerciseControl::get_frozen_behavior);
	ClassDB::bind_method(D_METHOD("get_simulation_time"), &DisExerciseControl::get_simulation_time);
	ClassDB::bind_method(D_METHOD("get_scheduled_time"), &DisExerciseControl::get_scheduled_time);

	DIS_BIND_PROPERTY(DisExerciseControl, NODE_PATH, network_path);
	DIS_BIND_PROPERTY(DisExerciseControl, BOOL, auto_acknowledge);
	DIS_BIND_PROPERTY(DisExerciseControl, BOOL, pause_tree);
	DIS_BIND_PROPERTY(DisExerciseControl, BOOL, remove_entities);
	DIS_BIND_PROPERTY(DisExerciseControl, BOOL, start_stopped);
	DIS_BIND_PROPERTY(DisExerciseControl, FLOAT, retry_interval);
	DIS_BIND_PROPERTY(DisExerciseControl, INT, max_retries);

	ADD_SIGNAL(MethodInfo("state_changed", PropertyInfo(Variant::INT, "state")));
	ADD_SIGNAL(MethodInfo("started", PropertyInfo(Variant::OBJECT, "pdu", PROPERTY_HINT_RESOURCE_TYPE, "DisStartResumePdu")));
	ADD_SIGNAL(MethodInfo("stopped", PropertyInfo(Variant::OBJECT, "pdu", PROPERTY_HINT_RESOURCE_TYPE, "DisStopFreezePdu")));
	ADD_SIGNAL(MethodInfo("create_entity_requested", PropertyInfo(Variant::OBJECT, "pdu", PROPERTY_HINT_RESOURCE_TYPE,
															  "DisCreateEntityPdu")));
	ADD_SIGNAL(MethodInfo("remove_entity_requested", PropertyInfo(Variant::VECTOR3I, "entity_id"),
			PropertyInfo(Variant::OBJECT, "pdu", PROPERTY_HINT_RESOURCE_TYPE, "DisRemoveEntityPdu")));
	ADD_SIGNAL(MethodInfo("acknowledged", PropertyInfo(Variant::INT, "request_id"), PropertyInfo(Variant::INT, "response_flag"),
			PropertyInfo(Variant::OBJECT, "pdu", PROPERTY_HINT_RESOURCE_TYPE, "DisAcknowledgePdu")));
	ADD_SIGNAL(MethodInfo("request_failed", PropertyInfo(Variant::INT, "request_id")));

	BIND_ENUM_CONSTANT(STATE_RUNNING);
	BIND_ENUM_CONSTANT(STATE_STOPPED);
}

void DisExerciseControl::_notification(int p_what) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (pause_tree) {
				// Keep listening (and counting time) while the rest of the scene is paused.
				set_process_mode(PROCESS_MODE_ALWAYS);
				DisNetwork *network = get_network();
				if (network != nullptr) {
					network->set_process_mode(PROCESS_MODE_ALWAYS);
				}
			}
			connect_network();
			set_process(true);
			if (start_stopped) {
				state = STATE_RUNNING; // so that stop() reports the change
				stop(0);
			}
		} break;
		case NOTIFICATION_PROCESS: {
			const double delta = get_process_delta_time();
			if (state == STATE_RUNNING || (frozen_behavior & DisStopFreezePdu::FROZEN_RUN_SIMULATION_CLOCK)) {
				simulation_time += delta;
			}
			const double time = now();
			if (pending.active && time >= pending.at) {
				pending.active = false;
				apply(pending.state, pending.pdu);
				pending.pdu.unref();
			}
			process_requests(time);
		} break;
		case NOTIFICATION_EXIT_TREE:
			disconnect_network();
			break;
	}
}

double DisExerciseControl::now() {
	return Time::get_singleton()->get_unix_time_from_system();
}

DisNetwork *DisExerciseControl::get_network() {
	return dis::find_node<DisNetwork>(this, network_path, "dis_network");
}

void DisExerciseControl::set_network_path(const NodePath &value) {
	network_path = value;
	if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint()) {
		connect_network();
	}
}

void DisExerciseControl::connect_network() {
	disconnect_network();
	DisNetwork *network = get_network();
	if (network == nullptr) {
		UtilityFunctions::push_warning("DisExerciseControl ", get_name(), ": no DisNetwork in the scene");
		return;
	}
	network->connect("start_resume_received", callable_mp(this, &DisExerciseControl::on_start_resume));
	network->connect("stop_freeze_received", callable_mp(this, &DisExerciseControl::on_stop_freeze));
	network->connect("remove_entity_received", callable_mp(this, &DisExerciseControl::on_remove_entity));
	network->connect("create_entity_received", callable_mp(this, &DisExerciseControl::on_create_entity));
	network->connect("acknowledge_received", callable_mp(this, &DisExerciseControl::on_acknowledge));
	connected_network = ObjectID(network->get_instance_id());
}

void DisExerciseControl::disconnect_network() {
	DisNetwork *network = Object::cast_to<DisNetwork>(ObjectDB::get_instance(connected_network));
	connected_network = ObjectID();
	if (network == nullptr) {
		return;
	}
	const Callable callables[] = {
		callable_mp(this, &DisExerciseControl::on_start_resume),
		callable_mp(this, &DisExerciseControl::on_stop_freeze),
		callable_mp(this, &DisExerciseControl::on_remove_entity),
		callable_mp(this, &DisExerciseControl::on_create_entity),
		callable_mp(this, &DisExerciseControl::on_acknowledge),
	};
	const char *signals[] = { "start_resume_received", "stop_freeze_received", "remove_entity_received",
		"create_entity_received", "acknowledge_received" };
	for (int i = 0; i < 5; i++) {
		if (network->is_connected(signals[i], callables[i])) {
			network->disconnect(signals[i], callables[i]);
		}
	}
}

Vector3i DisExerciseControl::own_id(DisNetwork *network) const {
	return Vector3i(network->get_site_id(), network->get_application_id(), 0);
}

bool DisExerciseControl::already_handled(const Ref<DisSimulationManagementPdu> &pdu) {
	const uint64_t key = request_key(pdu);
	if (std::find(handled.begin(), handled.end(), key) != handled.end()) {
		return true;
	}
	handled.push_back(key);
	if (handled.size() > HANDLED_MEMORY) {
		handled.pop_front();
	}
	return false;
}

void DisExerciseControl::acknowledge(DisNetwork *network, const Ref<DisSimulationManagementPdu> &request, int flag,
		int response) {
	if (!auto_acknowledge || network == nullptr || !network->is_running()) {
		return;
	}
	Ref<DisAcknowledgePdu> ack;
	ack.instantiate();
	ack->set_originating_id(own_id(network));
	ack->set_receiving_id(request->get_originating_id());
	ack->set_request_id(request->get_request_id());
	ack->set_acknowledge_flag(flag);
	ack->set_response_flag(response);
	ack->set_timestamp(DisPdu::timestamp_from_seconds(std::fmod(now(), 3600.0), true));
	network->send_pdu(ack);
}

void DisExerciseControl::schedule(State target, double at, const Ref<DisSimulationManagementPdu> &pdu) {
	const double time = now();
	if (at > time && at - time > MAX_SCHEDULE_AHEAD) {
		UtilityFunctions::push_warning(vformat("DisExerciseControl: requested time is %.0f s ahead; acting at once "
											   "(are the clocks synchronised?)", at - time));
	}
	if (at > time && at - time <= MAX_SCHEDULE_AHEAD) {
		pending.active = true;
		pending.state = target;
		pending.at = at;
		pending.pdu = pdu;
		return;
	}
	pending.active = false;
	pending.pdu.unref();
	apply(target, pdu);
}

void DisExerciseControl::apply(State target, const Ref<DisSimulationManagementPdu> &pdu) {
	const State previous = state;
	if (target == STATE_RUNNING) {
		const Ref<DisStartResumePdu> start_pdu = pdu;
		if (start_pdu.is_valid()) {
			simulation_time = start_pdu->get_simulation_time();
		}
		state = STATE_RUNNING;
		frozen_behavior = 0;
		if (pause_tree && is_inside_tree()) {
			get_tree()->set_pause(false);
		}
		emit_signal("started", pdu);
	} else {
		const Ref<DisStopFreezePdu> stop_pdu = pdu;
		if (stop_pdu.is_valid()) {
			frozen_behavior = stop_pdu->get_frozen_behavior();
		}
		state = STATE_STOPPED;
		if (pause_tree && is_inside_tree()) {
			get_tree()->set_pause(true);
		}
		emit_signal("stopped", pdu);
	}
	if (state != previous) {
		emit_signal("state_changed", state);
	}
}

void DisExerciseControl::start() {
	pending.active = false;
	apply(STATE_RUNNING, Ref<DisSimulationManagementPdu>());
}

void DisExerciseControl::stop(int new_frozen_behavior) {
	pending.active = false;
	frozen_behavior = new_frozen_behavior;
	apply(STATE_STOPPED, Ref<DisSimulationManagementPdu>());
}

// ---------------------------------------------------------------------------------------------------------------
// Managed application

void DisExerciseControl::handle_pdu(const Ref<DisPduBase> &pdu) {
	ERR_FAIL_COND(pdu.is_null());
	switch (pdu->get_pdu_type()) {
		case DisPdu::PDU_TYPE_START_RESUME:
			on_start_resume(pdu);
			break;
		case DisPdu::PDU_TYPE_STOP_FREEZE:
			on_stop_freeze(pdu);
			break;
		case DisPdu::PDU_TYPE_REMOVE_ENTITY:
			on_remove_entity(pdu);
			break;
		case DisPdu::PDU_TYPE_CREATE_ENTITY:
			on_create_entity(pdu);
			break;
		case DisPdu::PDU_TYPE_ACKNOWLEDGE:
			on_acknowledge(pdu);
			break;
		default:
			break;
	}
}

void DisExerciseControl::on_start_resume(const Ref<DisStartResumePdu> &pdu) {
	DisNetwork *network = get_network();
	if (pdu.is_null() || network == nullptr || !pdu->is_addressed_to(network->get_site_id(), network->get_application_id())) {
		return;
	}
	const bool repeated = already_handled(pdu);
	acknowledge(network, pdu, DisAcknowledgePdu::ACKNOWLEDGE_START_RESUME, DisAcknowledgePdu::RESPONSE_ABLE_TO_COMPLY);
	if (repeated) {
		return; // the manager did not hear our first answer
	}
	const bool immediate = pdu->get_real_world_time_hour() == 0 && pdu->get_real_world_time_past_hour() == 0;
	schedule(STATE_RUNNING, immediate ? 0.0 : pdu->get_real_world_time(), pdu);
}

void DisExerciseControl::on_stop_freeze(const Ref<DisStopFreezePdu> &pdu) {
	DisNetwork *network = get_network();
	if (pdu.is_null() || network == nullptr || !pdu->is_addressed_to(network->get_site_id(), network->get_application_id())) {
		return;
	}
	const bool repeated = already_handled(pdu);
	acknowledge(network, pdu, DisAcknowledgePdu::ACKNOWLEDGE_STOP_FREEZE, DisAcknowledgePdu::RESPONSE_ABLE_TO_COMPLY);
	if (repeated) {
		return;
	}
	const bool immediate = pdu->get_real_world_time_hour() == 0 && pdu->get_real_world_time_past_hour() == 0;
	schedule(STATE_STOPPED, immediate ? 0.0 : pdu->get_real_world_time(), pdu);
}

void DisExerciseControl::on_remove_entity(const Ref<DisRemoveEntityPdu> &pdu) {
	DisNetwork *network = get_network();
	if (pdu.is_null() || network == nullptr || !pdu->is_addressed_to(network->get_site_id(), network->get_application_id())) {
		return;
	}
	const bool repeated = already_handled(pdu);
	const int wanted = pdu->get_receiving_id().z;
	int found = 0;
	const TypedArray<Node> nodes = get_tree()->get_nodes_in_group(DisEntity::GROUP);
	for (int64_t i = 0; i < nodes.size(); i++) {
		DisEntity *entity = Object::cast_to<DisEntity>(nodes[i]);
		if (entity == nullptr || entity->is_queued_for_deletion()) {
			continue;
		}
		const Vector3i id = entity->get_effective_entity_id();
		if (id.x != network->get_site_id() || id.y != network->get_application_id() || (wanted != ALL && id.z != wanted)) {
			continue;
		}
		found++;
		if (!repeated) {
			emit_signal("remove_entity_requested", id, pdu);
			if (remove_entities) {
				entity->queue_free();
			}
		}
	}
	acknowledge(network, pdu, DisAcknowledgePdu::ACKNOWLEDGE_REMOVE_ENTITY,
			found > 0 || repeated ? DisAcknowledgePdu::RESPONSE_ABLE_TO_COMPLY : DisAcknowledgePdu::RESPONSE_UNABLE_TO_COMPLY);
}

// ---------------------------------------------------------------------------------------------------------------
// Simulation manager

int64_t DisExerciseControl::send_request(const Ref<DisSimulationManagementPdu> &pdu, int acknowledge_flag) {
	DisNetwork *network = get_network();
	ERR_FAIL_NULL_V_MSG(network, 0, "DisExerciseControl: no DisNetwork");
	ERR_FAIL_COND_V_MSG(!network->is_running(), 0, "DisExerciseControl: the DisNetwork is not running");
	Request request;
	request.id = next_request_id;
	next_request_id = next_request_id % 0xFFFFFFFF + 1;
	request.acknowledge_flag = acknowledge_flag;
	request.receiving_id = pdu->get_receiving_id();
	request.to_everyone = request.receiving_id.x == ALL || request.receiving_id.y == ALL;
	pdu->set_originating_id(own_id(network));
	pdu->set_request_id(request.id);
	pdu->set_timestamp(DisPdu::timestamp_from_seconds(std::fmod(now(), 3600.0), true));
	request.pdu = pdu;
	network->send_pdu(pdu);
	request.sent = 1;
	// To everyone: one send, then wait as long as retries would take. To one application: resend until answered.
	request.next_time = now() + retry_interval * (request.to_everyone ? std::max(1, max_retries + 1) : 1);
	requests.push_back(request);
	return request.id;
}

int64_t DisExerciseControl::send_start_resume(const Vector3i &receiving_id, double real_world_time, double sim_time) {
	Ref<DisStartResumePdu> pdu;
	pdu.instantiate();
	pdu->set_receiving_id(receiving_id);
	if (real_world_time > 0.0) {
		pdu->set_real_world_time(real_world_time);
	}
	pdu->set_simulation_time(sim_time);
	return send_request(pdu, DisAcknowledgePdu::ACKNOWLEDGE_START_RESUME);
}

int64_t DisExerciseControl::send_stop_freeze(const Vector3i &receiving_id, int reason, int behavior, double real_world_time) {
	Ref<DisStopFreezePdu> pdu;
	pdu.instantiate();
	pdu->set_receiving_id(receiving_id);
	if (real_world_time > 0.0) {
		pdu->set_real_world_time(real_world_time);
	}
	pdu->set_reason(reason);
	pdu->set_frozen_behavior(behavior);
	return send_request(pdu, DisAcknowledgePdu::ACKNOWLEDGE_STOP_FREEZE);
}

void DisExerciseControl::on_create_entity(const Ref<DisCreateEntityPdu> &pdu) {
	DisNetwork *network = get_network();
	if (pdu.is_null() || network == nullptr || !pdu->is_addressed_to(network->get_site_id(), network->get_application_id())) {
		return;
	}
	const bool handled_by_game = !get_signal_connection_list("create_entity_requested").is_empty();
	if (!already_handled(pdu) && handled_by_game) {
		emit_signal("create_entity_requested", pdu);
	}
	acknowledge(network, pdu, DisAcknowledgePdu::ACKNOWLEDGE_CREATE_ENTITY,
			handled_by_game ? DisAcknowledgePdu::RESPONSE_ABLE_TO_COMPLY : DisAcknowledgePdu::RESPONSE_UNABLE_TO_COMPLY);
}

int64_t DisExerciseControl::send_create_entity(const Vector3i &receiving_id) {
	Ref<DisCreateEntityPdu> pdu;
	pdu.instantiate();
	pdu->set_receiving_id(receiving_id);
	return send_request(pdu, DisAcknowledgePdu::ACKNOWLEDGE_CREATE_ENTITY);
}

int64_t DisExerciseControl::send_remove_entity(const Vector3i &entity_id) {
	Ref<DisRemoveEntityPdu> pdu;
	pdu.instantiate();
	pdu->set_receiving_id(entity_id);
	return send_request(pdu, DisAcknowledgePdu::ACKNOWLEDGE_REMOVE_ENTITY);
}

void DisExerciseControl::on_acknowledge(const Ref<DisAcknowledgePdu> &pdu) {
	DisNetwork *network = get_network();
	if (pdu.is_null() || network == nullptr) {
		return;
	}
	const Vector3i to = pdu->get_receiving_id();
	if (to.x != network->get_site_id() || to.y != network->get_application_id()) {
		return; // an answer to some other manager
	}
	for (size_t i = 0; i < requests.size(); i++) {
		Request &request = requests[i];
		if (request.id != pdu->get_request_id() || request.acknowledge_flag != pdu->get_acknowledge_flag()) {
			continue;
		}
		request.answers++;
		const int64_t id = request.id;
		if (!request.to_everyone) {
			requests.erase(requests.begin() + static_cast<std::ptrdiff_t>(i));
		}
		emit_signal("acknowledged", id, pdu->get_response_flag(), pdu);
		return;
	}
}

void DisExerciseControl::process_requests(double time) {
	DisNetwork *network = get_network();
	std::vector<int64_t> failed;
	for (size_t i = 0; i < requests.size();) {
		Request &request = requests[i];
		if (time < request.next_time) {
			i++;
			continue;
		}
		if (!request.to_everyone && request.sent <= max_retries && network != nullptr && network->is_running()) {
			network->send_pdu(request.pdu);
			request.sent++;
			request.next_time = time + retry_interval;
			i++;
			continue;
		}
		if (request.answers == 0) {
			failed.push_back(request.id);
		}
		requests.erase(requests.begin() + static_cast<std::ptrdiff_t>(i));
	}
	for (const int64_t id : failed) {
		emit_signal("request_failed", id);
	}
}

Array DisExerciseControl::get_pending_requests() const {
	Array out;
	for (const Request &request : requests) {
		Dictionary info;
		info["request_id"] = request.id;
		info["acknowledge_flag"] = request.acknowledge_flag;
		info["receiving_id"] = request.receiving_id;
		info["sent"] = request.sent;
		info["answers"] = request.answers;
		out.push_back(info);
	}
	return out;
}
