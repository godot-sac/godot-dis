#include "dis_entity.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "entity/scene_lookup.h"
#include "geo/dis_geo.h"
#include "geo/dis_geo_origin.h"
#include "network/dis_network.h"
#include "pdu/dis_entity_state_pdu.h"
#include "pdu/dis_pdu.h"

using namespace godot;
using dis::geo::Mat3;
using dis::geo::Vec3;

namespace {

// Time constants of the low-pass filters on the estimated motion (seconds).
constexpr double VELOCITY_SMOOTHING = 0.1;
constexpr double ACCELERATION_SMOOTHING = 0.25;

// Entity numbers given to entities that leave entity_id.z at 0, unique within this process.
int next_entity_number = 1;

Mat3 mat3_from_basis(const Basis &b) {
	return Mat3::from_columns(dis::geo::vec3_from_vector3(b.get_column(0)), dis::geo::vec3_from_vector3(b.get_column(1)),
			dis::geo::vec3_from_vector3(b.get_column(2)));
}

// Godot node axes (+X right, +Y up, +Z back) -> DIS body axes (+X forward, +Y right, +Z down).
Vec3 godot_axes_to_body(const Vector3 &v) {
	return { -v.z, v.x, -v.y };
}

// Rounds to what a float32 field on the wire can hold, so our copy of the sent state matches the receivers'.
Vec3 as_float32(const Vec3 &v) {
	return { static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z) };
}

} // namespace

void DisEntity::_bind_methods() {
	ClassDB::bind_method(D_METHOD("process_entity", "now"), &DisEntity::process_entity);
	ClassDB::bind_method(D_METHOD("send_now"), &DisEntity::send_now);
	ClassDB::bind_method(D_METHOD("create_pdu"), &DisEntity::create_pdu);
	ClassDB::bind_method(D_METHOD("get_effective_entity_id"), &DisEntity::get_effective_entity_id);
	ClassDB::bind_method(D_METHOD("get_published_count"), &DisEntity::get_published_count);

#define DIS_PROPERTY_HINT(variant_type, name, hint, hint_string)                         \
	ClassDB::bind_method(D_METHOD("set_" #name, "value"), &DisEntity::set_##name);     \
	ClassDB::bind_method(D_METHOD("get_" #name), &DisEntity::get_##name);              \
	ADD_PROPERTY(PropertyInfo(Variant::variant_type, #name, hint, hint_string), "set_" #name, "get_" #name)
#define DIS_PROPERTY(variant_type, name) DIS_PROPERTY_HINT(variant_type, name, PROPERTY_HINT_NONE, "")

	ADD_GROUP("Scene", "");
	DIS_PROPERTY_HINT(NODE_PATH, network_path, PROPERTY_HINT_NODE_PATH_VALID_TYPES, "DisNetwork");
	DIS_PROPERTY_HINT(NODE_PATH, origin_path, PROPERTY_HINT_NODE_PATH_VALID_TYPES, "DisGeoOrigin");
	ADD_GROUP("Entity", "");
	DIS_PROPERTY(VECTOR3I, entity_id);
	DIS_PROPERTY_HINT(INT, force_id, PROPERTY_HINT_ENUM, "Other,Friendly,Opposing,Neutral");
	DIS_PROPERTY(PACKED_INT32_ARRAY, entity_type);
	DIS_PROPERTY(STRING, marking);
	DIS_PROPERTY(INT, appearance);
	DIS_PROPERTY(INT, capabilities);
	ADD_GROUP("Publishing", "");
	ClassDB::bind_method(D_METHOD("set_publishing", "value"), &DisEntity::set_publishing);
	ClassDB::bind_method(D_METHOD("is_publishing"), &DisEntity::is_publishing);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "publishing"), "set_publishing", "is_publishing");
	DIS_PROPERTY(BOOL, auto_update);
	DIS_PROPERTY_HINT(INT, dr_algorithm, PROPERTY_HINT_ENUM,
			"Other:0,Static:1,FPW:2,RPW:3,RVW:4,FVW:5,FPB:6,RPB:7,RVB:8,FVB:9");
	DIS_PROPERTY_HINT(FLOAT, heartbeat_interval, PROPERTY_HINT_RANGE, "0.1,60,0.1,suffix:s");
	DIS_PROPERTY_HINT(FLOAT, position_threshold, PROPERTY_HINT_RANGE, "0.01,100,0.01,suffix:m");
	DIS_PROPERTY_HINT(FLOAT, orientation_threshold, PROPERTY_HINT_RANGE, "0.1,90,0.1,radians_as_degrees");
	ADD_GROUP("Motion", "");
	DIS_PROPERTY_HINT(INT, velocity_source, PROPERTY_HINT_ENUM, "Estimated,Manual");
	DIS_PROPERTY_HINT(VECTOR3, linear_velocity, PROPERTY_HINT_NONE, "suffix:m/s");
	DIS_PROPERTY_HINT(VECTOR3, linear_acceleration, PROPERTY_HINT_NONE, "suffix:m/s²");
	DIS_PROPERTY_HINT(VECTOR3, angular_velocity, PROPERTY_HINT_NONE, "suffix:rad/s");
#undef DIS_PROPERTY
#undef DIS_PROPERTY_HINT

	ADD_SIGNAL(MethodInfo("published", PropertyInfo(Variant::OBJECT, "pdu", PROPERTY_HINT_RESOURCE_TYPE,
											   "DisEntityStatePdu")));

	BIND_ENUM_CONSTANT(VELOCITY_ESTIMATED);
	BIND_ENUM_CONSTANT(VELOCITY_MANUAL);
	BIND_CONSTANT(APPEARANCE_DEACTIVATED);
}

DisEntity::DisEntity() {
	entity_type.resize(7);
	entity_type.fill(0);
}

void DisEntity::_notification(int p_what) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	switch (p_what) {
		case NOTIFICATION_READY:
			set_physics_process(auto_update);
			break;
		case NOTIFICATION_PHYSICS_PROCESS:
			physics_time += get_physics_process_delta_time();
			process_entity(physics_time);
			break;
		case NOTIFICATION_EXIT_TREE:
			// Tell receivers we are gone (they remove the entity instead of waiting for the timeout).
			if (has_sent && publishing) {
				DisNetwork *network = get_network();
				if (network != nullptr && network->is_running()) {
					send(sent_state, physics_time, appearance | APPEARANCE_DEACTIVATED);
				}
			}
			has_sample = false;
			has_sent = false;
			break;
	}
}

DisNetwork *DisEntity::get_network() {
	DisNetwork *network = Object::cast_to<DisNetwork>(ObjectDB::get_instance(network_id));
	if (network == nullptr) {
		network = dis::find_node<DisNetwork>(this, network_path, "dis_network");
		network_id = network != nullptr ? ObjectID(network->get_instance_id()) : ObjectID();
	}
	return network;
}

DisGeoOrigin *DisEntity::get_origin() {
	DisGeoOrigin *origin = dis::find_node<DisGeoOrigin>(this, origin_path, DisGeoOrigin::GROUP);
	if (origin == nullptr && !warned_no_origin) {
		warned_no_origin = true;
		UtilityFunctions::push_warning("DisEntity ", get_name(), ": no DisGeoOrigin in the scene; nothing is published");
	}
	return origin;
}

Vector3i DisEntity::get_effective_entity_id() {
	DisNetwork *network = get_network();
	Vector3i id = entity_id;
	if (id.x == 0) {
		id.x = network != nullptr ? network->get_site_id() : 1;
	}
	if (id.y == 0) {
		id.y = network != nullptr ? network->get_application_id() : 1;
	}
	if (id.z == 0) {
		if (assigned_number == 0) {
			assigned_number = next_entity_number;
			next_entity_number = next_entity_number % 65534 + 1;
		}
		id.z = assigned_number;
	}
	return id;
}

void DisEntity::sample_motion(double now) {
	const Vector3 position = get_global_position();
	const Basis basis = get_global_basis().orthonormalized();
	const double dt = now - sample_time;
	if (velocity_source == VELOCITY_ESTIMATED && has_sample && dt > 1e-6) {
		const Vector3 measured_velocity = (position - sample_position) / dt;
		const Vector3 previous_velocity = linear_velocity;
		linear_velocity += (measured_velocity - linear_velocity) * (1.0 - std::exp(-dt / VELOCITY_SMOOTHING));
		const Vector3 measured_acceleration = (linear_velocity - previous_velocity) / dt;
		linear_acceleration +=
				(measured_acceleration - linear_acceleration) * (1.0 - std::exp(-dt / ACCELERATION_SMOOTHING));
		// Rotation between the two samples, in the node's own axes.
		const Vec3 w = dis::dr::body_rates_between(mat3_from_basis(sample_basis), mat3_from_basis(basis), dt);
		const Vector3 measured_angular(w.x, w.y, w.z);
		angular_velocity += (measured_angular - angular_velocity) * (1.0 - std::exp(-dt / VELOCITY_SMOOTHING));
	}
	if (dt > 1e-6 || !has_sample) {
		has_sample = true;
		sample_time = now;
		sample_position = position;
		sample_basis = basis;
	}
}

dis::dr::State DisEntity::current_state(DisGeoOrigin *origin) const {
	const Mat3 &enu_to_ecef = origin->get_frame().rotation();
	dis::dr::State s;
	s.algorithm = dr_algorithm;
	s.position = origin->local_to_ecef_vec3(get_global_position());
	s.orientation = origin->basis_to_body_matrix(get_global_basis());
	s.velocity = as_float32(enu_to_ecef * dis::geo::godot_to_enu(dis::geo::vec3_from_vector3(linear_velocity)));
	s.acceleration =
			as_float32(enu_to_ecef * dis::geo::godot_to_enu(dis::geo::vec3_from_vector3(linear_acceleration)));
	s.angular_velocity = as_float32(godot_axes_to_body(angular_velocity));
	return s;
}

Ref<DisEntityStatePdu> DisEntity::make_pdu(const dis::dr::State &state, int64_t pdu_appearance) {
	Ref<DisEntityStatePdu> pdu;
	pdu.instantiate();
	pdu->set_entity_id(get_effective_entity_id());
	pdu->set_force_id(force_id);
	pdu->set_entity_type(entity_type);
	pdu->set_marking(marking);
	pdu->set_appearance(pdu_appearance);
	pdu->set_capabilities(capabilities);
	pdu->set_location_ecef(dis::geo::packed_from_vec3(state.position));
	pdu->set_orientation(dis::geo::vector3_from_euler(dis::geo::matrix_to_euler(state.orientation)));
	pdu->set_linear_velocity(dis::geo::vector3_from_vec3(state.velocity));
	pdu->set_dr_algorithm(dr_algorithm);
	pdu->set_dr_linear_acceleration(dis::geo::vector3_from_vec3(state.acceleration));
	pdu->set_dr_angular_velocity(dis::geo::vector3_from_vec3(state.angular_velocity));
	const double unix_time = Time::get_singleton()->get_unix_time_from_system();
	pdu->set_timestamp(DisPdu::timestamp_from_seconds(std::fmod(unix_time, 3600.0), true));
	return pdu;
}

Error DisEntity::send(const dis::dr::State &state, double now, int64_t pdu_appearance) {
	const Ref<DisEntityStatePdu> pdu = make_pdu(state, pdu_appearance);
	sent_state = state;
	sent_time = now;
	has_sent = true;
	dirty = false;
	published_count++;
	emit_signal("published", pdu);
	DisNetwork *network = get_network();
	if (network == nullptr || !network->is_running()) {
		return ERR_UNCONFIGURED;
	}
	return network->send_pdu(pdu);
}

void DisEntity::process_entity(double now) {
	if (!is_inside_tree()) {
		return;
	}
	sample_motion(now);
	if (!publishing) {
		return;
	}
	DisGeoOrigin *origin = get_origin();
	if (origin == nullptr) {
		return;
	}
	const dis::dr::State state = current_state(origin);
	bool needed = !has_sent || dirty || now - sent_time >= heartbeat_interval;
	if (!needed) {
		// What the receivers show right now, from the last PDU.
		const dis::dr::Pose predicted = dis::dr::extrapolate(sent_state, now - sent_time);
		needed = (predicted.position - state.position).length() > position_threshold ||
				dis::dr::angle_between(predicted.orientation, state.orientation) > orientation_threshold;
	}
	if (needed) {
		send(state, now, appearance);
	}
}

Error DisEntity::send_now() {
	DisGeoOrigin *origin = get_origin();
	ERR_FAIL_NULL_V_MSG(origin, ERR_UNCONFIGURED, "DisEntity needs a DisGeoOrigin in the scene");
	return send(current_state(origin), has_sent ? std::max(physics_time, sent_time) : physics_time, appearance);
}

Ref<DisEntityStatePdu> DisEntity::create_pdu() {
	DisGeoOrigin *origin = get_origin();
	ERR_FAIL_NULL_V_MSG(origin, Ref<DisEntityStatePdu>(), "DisEntity needs a DisGeoOrigin in the scene");
	return make_pdu(current_state(origin), appearance);
}

void DisEntity::set_network_path(const NodePath &value) {
	network_path = value;
	network_id = ObjectID();
}

void DisEntity::set_entity_id(const Vector3i &value) {
	entity_id = value;
	dirty = true;
}

void DisEntity::set_force_id(int value) {
	force_id = value;
	dirty = true;
}

void DisEntity::set_entity_type(const PackedInt32Array &value) {
	entity_type = value;
	entity_type.resize(7);
	dirty = true;
}

void DisEntity::set_marking(const String &value) {
	marking = value;
	dirty = true;
}

void DisEntity::set_appearance(int64_t value) {
	dirty = dirty || value != appearance;
	appearance = value;
}

void DisEntity::set_capabilities(int64_t value) {
	dirty = dirty || value != capabilities;
	capabilities = value;
}

void DisEntity::set_dr_algorithm(int value) {
	dr_algorithm = value;
	dirty = true;
}

void DisEntity::set_auto_update(bool value) {
	auto_update = value;
	if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint()) {
		set_physics_process(value);
	}
}
