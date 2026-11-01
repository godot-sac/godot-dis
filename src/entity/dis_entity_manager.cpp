#include "dis_entity_manager.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "entity/dis_entity.h"
#include "entity/dis_entity_type_map.h"
#include "entity/scene_lookup.h"
#include "geo/dis_geo.h"
#include "geo/dis_geo_origin.h"
#include "network/dis_network.h"
#include "pdu/dis_entity_state_pdu.h"

using namespace godot;
using dis::geo::Vec3;

namespace {

double now_seconds() {
	return Time::get_singleton()->get_ticks_usec() / 1.0e6;
}

} // namespace

// DisRemoteEntity

void DisRemoteEntity::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_entity_id"), &DisRemoteEntity::get_entity_id);
	ClassDB::bind_method(D_METHOD("get_last_pdu"), &DisRemoteEntity::get_last_pdu);
	ClassDB::bind_method(D_METHOD("get_last_update_time"), &DisRemoteEntity::get_last_update_time);
	ClassDB::bind_method(D_METHOD("get_force_id"), &DisRemoteEntity::get_force_id);
	ClassDB::bind_method(D_METHOD("get_entity_type"), &DisRemoteEntity::get_entity_type);
	ClassDB::bind_method(D_METHOD("get_marking"), &DisRemoteEntity::get_marking);
	ClassDB::bind_method(D_METHOD("get_appearance"), &DisRemoteEntity::get_appearance);
	ClassDB::bind_method(D_METHOD("get_dr_algorithm"), &DisRemoteEntity::get_dr_algorithm);
	ClassDB::bind_method(D_METHOD("get_location_ecef"), &DisRemoteEntity::get_location_ecef);
	ClassDB::bind_method(D_METHOD("get_geodetic"), &DisRemoteEntity::get_geodetic);
	// Read-only properties (no setter) so they show in the remote scene tree inspector.
	const uint32_t read_only = PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_READ_ONLY;
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3I, "entity_id", PROPERTY_HINT_NONE, "", read_only), "", "get_entity_id");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "force_id", PROPERTY_HINT_NONE, "", read_only), "", "get_force_id");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "entity_type", PROPERTY_HINT_NONE, "", read_only), "",
			"get_entity_type");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "marking", PROPERTY_HINT_NONE, "", read_only), "", "get_marking");
}

int DisRemoteEntity::get_force_id() const {
	return last_pdu.is_valid() ? last_pdu->get_force_id() : 0;
}

PackedInt32Array DisRemoteEntity::get_entity_type() const {
	return last_pdu.is_valid() ? last_pdu->get_entity_type() : PackedInt32Array();
}

String DisRemoteEntity::get_marking() const {
	return last_pdu.is_valid() ? last_pdu->get_marking() : String();
}

int64_t DisRemoteEntity::get_appearance() const {
	return last_pdu.is_valid() ? last_pdu->get_appearance() : 0;
}

PackedFloat64Array DisRemoteEntity::get_location_ecef() const {
	return dis::geo::packed_from_vec3(current_ecef);
}

PackedFloat64Array DisRemoteEntity::get_geodetic() const {
	const dis::geo::Geodetic g = dis::geo::ecef_to_geodetic(current_ecef);
	return dis::geo::packed_from_vec3({ dis::geo::rad_to_deg(g.latitude), dis::geo::rad_to_deg(g.longitude), g.height });
}

// DisEntityManager

void DisEntityManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("handle_entity_state", "pdu", "now"), &DisEntityManager::handle_entity_state,
			DEFVAL(-1.0));
	ClassDB::bind_method(D_METHOD("process_entities", "now"), &DisEntityManager::process_entities);
	ClassDB::bind_method(D_METHOD("get_clock"), &DisEntityManager::get_clock);
	ClassDB::bind_method(D_METHOD("set_time_scale", "value"), &DisEntityManager::set_time_scale);
	ClassDB::bind_method(D_METHOD("get_time_scale"), &DisEntityManager::get_time_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time_scale", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater"), "set_time_scale",
			"get_time_scale");
	ClassDB::bind_method(D_METHOD("get_entity", "entity_id"), &DisEntityManager::get_entity);
	ClassDB::bind_method(D_METHOD("get_entities"), &DisEntityManager::get_entities);
	ClassDB::bind_method(D_METHOD("get_entity_count"), &DisEntityManager::get_entity_count);
	ClassDB::bind_method(D_METHOD("remove_entity", "entity_id"), &DisEntityManager::remove_entity);
	ClassDB::bind_method(D_METHOD("clear"), &DisEntityManager::clear);

#define DIS_PROPERTY_HINT(variant_type, name, hint, hint_string)                                \
	ClassDB::bind_method(D_METHOD("set_" #name, "value"), &DisEntityManager::set_##name);     \
	ClassDB::bind_method(D_METHOD("get_" #name), &DisEntityManager::get_##name);              \
	ADD_PROPERTY(PropertyInfo(Variant::variant_type, #name, hint, hint_string), "set_" #name, "get_" #name)
#define DIS_PROPERTY(variant_type, name) DIS_PROPERTY_HINT(variant_type, name, PROPERTY_HINT_NONE, "")

	DIS_PROPERTY_HINT(NODE_PATH, network_path, PROPERTY_HINT_NODE_PATH_VALID_TYPES, "DisNetwork");
	DIS_PROPERTY_HINT(NODE_PATH, origin_path, PROPERTY_HINT_NODE_PATH_VALID_TYPES, "DisGeoOrigin");
	DIS_PROPERTY_HINT(OBJECT, type_map, PROPERTY_HINT_RESOURCE_TYPE, "DisEntityTypeMap");
	DIS_PROPERTY_HINT(FLOAT, timeout, PROPERTY_HINT_RANGE, "0.5,120,0.5,suffix:s");
	DIS_PROPERTY(BOOL, dead_reckoning);
	DIS_PROPERTY_HINT(FLOAT, smoothing_time, PROPERTY_HINT_RANGE, "0,5,0.05,suffix:s");
	DIS_PROPERTY_HINT(FLOAT, smoothing_max_distance, PROPERTY_HINT_RANGE, "0,10000,1,suffix:m");
	DIS_PROPERTY(BOOL, auto_update);
	ADD_GROUP("Ground", "ground_");
	DIS_PROPERTY(NODE_PATH, ground_path);
	DIS_PROPERTY_HINT(INT, ground_clamp, PROPERTY_HINT_ENUM, "Disabled,Land,All");
	DIS_PROPERTY(BOOL, ground_align);
	DIS_PROPERTY_HINT(FLOAT, ground_align_distance, PROPERTY_HINT_RANGE, "0.5,50,0.5,suffix:m");
#undef DIS_PROPERTY
#undef DIS_PROPERTY_HINT

	ClassDB::bind_method(D_METHOD("is_ground_clamped", "entity"), &DisEntityManager::is_ground_clamped);
	BIND_ENUM_CONSTANT(GROUND_CLAMP_DISABLED);
	BIND_ENUM_CONSTANT(GROUND_CLAMP_LAND);
	BIND_ENUM_CONSTANT(GROUND_CLAMP_ALL);

	ADD_SIGNAL(MethodInfo("entity_appeared", PropertyInfo(Variant::OBJECT, "entity", PROPERTY_HINT_NODE_TYPE,
														 "DisRemoteEntity")));
	ADD_SIGNAL(MethodInfo("entity_updated", PropertyInfo(Variant::OBJECT, "entity", PROPERTY_HINT_NODE_TYPE,
														"DisRemoteEntity")));
	ADD_SIGNAL(MethodInfo("entity_removed",
			PropertyInfo(Variant::OBJECT, "entity", PROPERTY_HINT_NODE_TYPE, "DisRemoteEntity"),
			PropertyInfo(Variant::STRING, "reason")));
}

void DisEntityManager::_notification(int p_what) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	switch (p_what) {
		case NOTIFICATION_READY:
			connect_network();
			set_process(auto_update);
			break;
		case NOTIFICATION_PROCESS:
			process_entities(get_clock());
			break;
		case NOTIFICATION_EXIT_TREE:
			disconnect_network();
			break;
	}
}

uint64_t DisEntityManager::key_of(const Vector3i &id) {
	return (static_cast<uint64_t>(id.x & 0xFFFF) << 32) | (static_cast<uint64_t>(id.y & 0xFFFF) << 16) |
			static_cast<uint64_t>(id.z & 0xFFFF);
}

DisGeoOrigin *DisEntityManager::get_origin() {
	DisGeoOrigin *origin = dis::find_node<DisGeoOrigin>(this, origin_path, DisGeoOrigin::GROUP);
	if (origin == nullptr && !warned_no_origin) {
		warned_no_origin = true;
		UtilityFunctions::push_warning("DisEntityManager ", get_name(),
				": no DisGeoOrigin in the scene; remote entities are ignored");
	}
	return origin;
}

void DisEntityManager::connect_network() {
	disconnect_network();
	DisNetwork *network = dis::find_node<DisNetwork>(this, network_path, "dis_network");
	if (network == nullptr) {
		return; // PDUs can still be fed with handle_entity_state()
	}
	network->connect("entity_state_received", callable_mp(this, &DisEntityManager::on_entity_state));
	connected_network = ObjectID(network->get_instance_id());
}

void DisEntityManager::disconnect_network() {
	DisNetwork *network = Object::cast_to<DisNetwork>(ObjectDB::get_instance(connected_network));
	const Callable callable = callable_mp(this, &DisEntityManager::on_entity_state);
	if (network != nullptr && network->is_connected("entity_state_received", callable)) {
		network->disconnect("entity_state_received", callable);
	}
	connected_network = ObjectID();
}

void DisEntityManager::on_entity_state(const Ref<DisEntityStatePdu> &pdu) {
	handle_entity_state(pdu, get_clock());
}

void DisEntityManager::place(DisRemoteEntity *entity, DisGeoOrigin *origin, double now, bool smooth) {
	const double dt = std::max(0.0, now - entity->last_update_time);
	const dis::dr::Pose pose = dis::dr::extrapolate(entity->state, dt);
	entity->current_ecef = pose.position;
	Vector3 position = origin->ecef_vec3_to_local(pose.position);
	Quaternion rotation = origin->body_matrix_to_basis(pose.orientation).get_rotation_quaternion();
	if (smooth && smoothing_time > 0.0) {
		const double k = 1.0 - (now - entity->last_update_time) / smoothing_time;
		if (k > 0.0) {
			position += entity->offset_position * k;
			rotation = (Quaternion().slerp(entity->offset_rotation, k) * rotation).normalized();
		}
	}
	if (clamps(entity)) {
		if (Object *ground = get_ground()) {
			apply_ground(ground, position, rotation);
		}
	}
	entity->set_global_transform(Transform3D(Basis(rotation), position));
}

Object *DisEntityManager::get_ground() {
	if (ground_path.is_empty() || !is_inside_tree()) {
		return nullptr;
	}
	Node *ground = get_node_or_null(ground_path);
	if (ground != nullptr && !ground->has_method("height_at")) {
		if (!warned_no_height_at) {
			warned_no_height_at = true;
			UtilityFunctions::push_warning("DisEntityManager ", get_name(), ": ground node ", ground->get_name(),
					" has no height_at(x, z); entities are not clamped");
		}
		return nullptr;
	}
	return ground;
}

bool DisEntityManager::clamps(const DisRemoteEntity *entity) const {
	if (ground_clamp == GROUND_CLAMP_DISABLED || entity->last_pdu.is_null()) {
		return false;
	}
	if (ground_clamp == GROUND_CLAMP_ALL) {
		return true;
	}
	// Entity type: kind, domain, ... Kinds 1 platform, 3 life form, 5 cultural feature; domain 1 is land.
	const PackedInt32Array type = entity->last_pdu->get_entity_type();
	if (type.size() < 2 || type[1] != 1) {
		return false;
	}
	return type[0] == 1 || type[0] == 3 || type[0] == 5;
}

bool DisEntityManager::is_ground_clamped(Object *entity) const {
	const DisRemoteEntity *remote = Object::cast_to<DisRemoteEntity>(entity);
	return remote != nullptr && clamps(remote);
}

void DisEntityManager::apply_ground(Object *ground, Vector3 &position, Quaternion &rotation) const {
	// Object::call goes through Variant, so any node (GDScript, C#, C++) providing height_at works.
	auto height = [ground](real_t x, real_t z) { return static_cast<double>(ground->call("height_at", x, z)); };
	const double h = height(position.x, position.z);
	if (!std::isfinite(h)) {
		return; // off the terrain: keep the reported height
	}
	position.y = static_cast<real_t>(h);
	if (!ground_align) {
		return;
	}
	const real_t d = static_cast<real_t>(ground_align_distance);
	const double dx = height(position.x + d, position.z) - height(position.x - d, position.z);
	const double dz = height(position.x, position.z + d) - height(position.x, position.z - d);
	if (!std::isfinite(dx) || !std::isfinite(dz)) {
		return;
	}
	const Vector3 up = Vector3(static_cast<real_t>(-dx), 2.0f * d, static_cast<real_t>(-dz)).normalized();
	// Keep the heading: project the reported forward (-Z) onto the ground plane.
	const Vector3 forward = Basis(rotation).xform(Vector3(0, 0, -1));
	Vector3 along = forward - up * forward.dot(up);
	if (along.length_squared() < 1e-8f) {
		return; // pointing straight up or down; leave it
	}
	rotation = Basis::looking_at(along.normalized(), up).get_rotation_quaternion();
}

void DisEntityManager::handle_entity_state(const Ref<DisEntityStatePdu> &pdu, double now) {
	ERR_FAIL_COND(pdu.is_null());
	if (now < 0.0) {
		now = get_clock();
	}
	DisGeoOrigin *origin = get_origin();
	if (origin == nullptr || !is_inside_tree()) {
		return;
	}
	const Vector3i id = pdu->get_entity_id();
	const uint64_t key = key_of(id);
	DisRemoteEntity *entity = find_entity(id);

	if (pdu->get_appearance() & DisEntity::APPEARANCE_DEACTIVATED) {
		if (entity != nullptr) {
			remove(key, entity, "deactivated");
		}
		return;
	}

	dis::dr::State state;
	state.algorithm = dead_reckoning ? pdu->get_dr_algorithm() : dis::dr::ALGORITHM_STATIC;
	state.position = dis::geo::vec3_from_packed(pdu->get_location_ecef());
	state.velocity = dis::geo::vec3_from_vector3(pdu->get_linear_velocity());
	state.acceleration = dis::geo::vec3_from_vector3(pdu->get_dr_linear_acceleration());
	state.orientation = dis::geo::euler_to_matrix(dis::geo::euler_from_vector3(pdu->get_orientation()));
	state.angular_velocity = dis::geo::vec3_from_vector3(pdu->get_dr_angular_velocity());

	const bool is_new = entity == nullptr;
	Transform3D shown;
	if (is_new) {
		entity = memnew(DisRemoteEntity);
		entity->entity_id = id;
		entity->set_name(vformat("Entity_%d_%d_%d", id.x, id.y, id.z));
		if (type_map.is_valid()) {
			const Ref<PackedScene> scene = type_map->find_scene(pdu->get_entity_type());
			if (scene.is_valid()) {
				entity->add_child(scene->instantiate());
			}
		}
		add_child(entity);
		entities[key] = ObjectID(entity->get_instance_id());
	} else {
		// Where the entity is shown *now* (old report, dead reckoned and smoothed to this moment). The node still
		// holds the pose of the last frame; fading from that one made every update stall for a frame and then
		// catch up (a fast entity visibly jerked once per update).
		if (smoothing_time > 0.0) {
			place(entity, origin, now, true);
		}
		shown = entity->get_global_transform();
	}

	entity->last_pdu = pdu;
	entity->state = state;
	entity->last_update_time = now;
	entity->offset_position = Vector3();
	entity->offset_rotation = Quaternion();
	place(entity, origin, now, false);

	if (!is_new) {
		const Transform3D predicted = entity->get_global_transform();
		const Vector3 jump = shown.origin - predicted.origin;
		// Real teleports are rare; several big jumps in a row usually mean two simulations publish the same
		// entity ID, and the entity flips between their positions.
		if (jump.length() > smoothing_max_distance) {
			entity->jumps_in_a_row += 1;
			if (entity->jumps_in_a_row >= 3 && !entity->warned_jumps) {
				entity->warned_jumps = true;
				UtilityFunctions::push_warning(vformat(
						"DisEntityManager: entity %d:%d:%d (%s) jumped %d m on %d updates in a row; two simulations "
						"may be publishing the same entity ID",
						id.x, id.y, id.z, pdu->get_marking(), static_cast<int64_t>(jump.length()),
						entity->jumps_in_a_row));
			}
		} else {
			entity->jumps_in_a_row = 0;
		}
	}
	if (!is_new && smoothing_time > 0.0) {
		// Fade from where the entity was shown to the new prediction instead of jumping.
		const Transform3D predicted = entity->get_global_transform();
		const Vector3 jump = shown.origin - predicted.origin;
		if (jump.length() <= smoothing_max_distance) {
			entity->offset_position = jump;
			entity->offset_rotation = (shown.basis.get_rotation_quaternion() *
					predicted.basis.get_rotation_quaternion().inverse())
											  .normalized();
			entity->set_global_transform(shown);
		}
	}
	emit_signal(is_new ? "entity_appeared" : "entity_updated", entity);
}

double DisEntityManager::get_clock() const {
	return clock_base + (now_seconds() - clock_ticks_base) * time_scale;
}

void DisEntityManager::set_time_scale(double value) {
	clock_base = get_clock();
	clock_ticks_base = now_seconds();
	time_scale = value < 0.0 ? 0.0 : value;
}

void DisEntityManager::process_entities(double now) {
	DisGeoOrigin *origin = get_origin();
	std::vector<std::pair<uint64_t, ObjectID>> expired;
	for (const auto &[key, object_id] : entities) {
		DisRemoteEntity *entity = Object::cast_to<DisRemoteEntity>(ObjectDB::get_instance(object_id));
		if (entity == nullptr || now - entity->last_update_time > timeout) {
			expired.emplace_back(key, object_id);
		} else if (origin != nullptr) {
			place(entity, origin, now, true);
		}
	}
	for (const auto &[key, object_id] : expired) {
		DisRemoteEntity *entity = Object::cast_to<DisRemoteEntity>(ObjectDB::get_instance(object_id));
		if (entity == nullptr) {
			entities.erase(key); // freed by someone else
		} else {
			remove(key, entity, "timeout");
		}
	}
}

void DisEntityManager::remove(uint64_t key, DisRemoteEntity *entity, const String &reason) {
	entities.erase(key);
	emit_signal("entity_removed", entity, reason);
	entity->queue_free();
}

DisRemoteEntity *DisEntityManager::find_entity(const Vector3i &entity_id) const {
	const auto it = entities.find(key_of(entity_id));
	if (it == entities.end()) {
		return nullptr;
	}
	return Object::cast_to<DisRemoteEntity>(ObjectDB::get_instance(it->second));
}

Object *DisEntityManager::get_entity(const Vector3i &entity_id) const {
	return find_entity(entity_id);
}

Array DisEntityManager::get_entities() const {
	Array result;
	for (const auto &[key, object_id] : entities) {
		if (Object *entity = ObjectDB::get_instance(object_id)) {
			result.push_back(entity);
		}
	}
	return result;
}

void DisEntityManager::remove_entity(const Vector3i &entity_id) {
	if (DisRemoteEntity *entity = find_entity(entity_id)) {
		remove(key_of(entity_id), entity, "removed");
	}
}

void DisEntityManager::clear() {
	const std::unordered_map<uint64_t, ObjectID> all = entities;
	for (const auto &[key, object_id] : all) {
		if (DisRemoteEntity *entity = Object::cast_to<DisRemoteEntity>(ObjectDB::get_instance(object_id))) {
			remove(key, entity, "removed");
		}
	}
	entities.clear();
}

void DisEntityManager::set_network_path(const NodePath &value) {
	network_path = value;
	if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint()) {
		connect_network();
	}
}

void DisEntityManager::set_type_map(const Ref<DisEntityTypeMap> &value) {
	type_map = value;
}

Ref<DisEntityTypeMap> DisEntityManager::get_type_map() const {
	return type_map;
}

void DisEntityManager::set_auto_update(bool value) {
	auto_update = value;
	if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint()) {
		set_process(value);
	}
}
