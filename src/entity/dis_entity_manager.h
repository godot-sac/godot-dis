#pragma once

#include <cstdint>
#include <unordered_map>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include "entity/dead_reckoning.h"
#include "entity/dis_entity_type_map.h"
#include "pdu/dis_entity_state_update_pdu.h"
#include "pdu/dis_entity_state_pdu.h"

class DisGeoOrigin;
class DisNetwork;

// A remote entity, created and moved by DisEntityManager. The scene chosen by the type map (if any) is its child.
class DisRemoteEntity : public godot::Node3D {
	GDCLASS(DisRemoteEntity, godot::Node3D)

	friend class DisEntityManager;

	godot::Vector3i entity_id;
	godot::Ref<DisEntityStatePdu> last_pdu;
	double last_update_time = 0.0; // manager clock, seconds
	dis::dr::State state; // last reported, in ECEF
	dis::geo::Vec3 current_ecef; // dead-reckoned position at the last update of the node

	// Smoothing: the jump between the old prediction and the new report fades out over smoothing_time.
	godot::Vector3 offset_position;
	godot::Quaternion offset_rotation;

	// Updates in a row that jumped farther than smoothing_max_distance (see DisEntityManager).
	int jumps_in_a_row = 0;
	bool warned_jumps = false;

protected:
	static void _bind_methods();

public:
	godot::Vector3i get_entity_id() const { return entity_id; }
	godot::Ref<DisEntityStatePdu> get_last_pdu() const { return last_pdu; }
	double get_last_update_time() const { return last_update_time; }
	int get_force_id() const;
	godot::PackedInt32Array get_entity_type() const;
	godot::String get_marking() const;
	int64_t get_appearance() const;
	int get_dr_algorithm() const { return state.algorithm; }
	// Dead-reckoned position (what the node shows, without smoothing).
	godot::PackedFloat64Array get_location_ecef() const;
	// [latitude, longitude, height] of get_location_ecef().
	godot::PackedFloat64Array get_geodetic() const;
};

// Shows the entities other simulations publish: listens to a DisNetwork, creates a DisRemoteEntity child for
// each new entity, moves it every frame with dead reckoning (smoothing out the corrections), and removes it
// when it is deactivated or not heard from for `timeout` seconds.
// With a ground node (anything with `height_at(x, z) -> float`, global metres, NAN = no ground; e.g. godot-geo
// GeoTerrain) land entities are put on the local terrain and optionally tilted to its slope, because the sender's
// terrain is never exactly ours.
class DisEntityManager : public godot::Node3D {
	GDCLASS(DisEntityManager, godot::Node3D)

public:
	enum GroundClamp {
		GROUND_CLAMP_DISABLED,
		GROUND_CLAMP_LAND, // land platforms, life forms and cultural features
		GROUND_CLAMP_ALL,
	};

private:

	godot::NodePath network_path;
	godot::NodePath origin_path;
	godot::Ref<DisEntityTypeMap> type_map;
	double timeout = 12.0;
	// The clock entities are dead reckoned on: real seconds, scaled by time_scale (DisPlayer sets it to the
	// playback speed, 0 while paused). clock = clock_base + (ticks - clock_ticks_base) * time_scale.
	double time_scale = 1.0;
	double clock_base = 0.0;
	bool warned_non_finite = false;
	// Remote entities are created by whoever sends to our port; this caps how many nodes they can make us create.
	int max_entities = 10000;
	int64_t dropped_count = 0; // Entity State PDUs of new entities not taken because of max_entities
	bool at_entity_limit = false;
	bool warned_entity_limit = false;
	double clock_ticks_base = 0.0;
	double smoothing_time = 0.5;
	double smoothing_max_distance = 100.0;
	bool dead_reckoning = true;
	bool auto_update = true;
	godot::NodePath ground_path;
	int ground_clamp = GROUND_CLAMP_LAND;
	bool ground_align = true;
	double ground_align_distance = 4.0; // half the footprint used to measure the slope, metres
	bool warned_no_height_at = false;

	std::unordered_map<uint64_t, godot::ObjectID> entities;
	godot::ObjectID connected_network;
	bool warned_no_origin = false;

	static uint64_t key_of(const godot::Vector3i &id);
	DisGeoOrigin *get_origin();
	void connect_network();
	void disconnect_network();
	void on_entity_state(const godot::Ref<DisEntityStatePdu> &pdu);
	void on_entity_state_update(const godot::Ref<DisEntityStateUpdatePdu> &pdu);
	// Places the node for time `now`; returns false if there is no origin.
	void place(DisRemoteEntity *entity, DisGeoOrigin *origin, double now, bool smooth);
	void remove(uint64_t key, DisRemoteEntity *entity, const godot::String &reason);
	godot::Object *get_ground();
	bool clamps(const DisRemoteEntity *entity) const;
	// Puts `position` on the ground and, with ground_align, tilts `rotation` to the slope keeping the heading.
	void apply_ground(godot::Object *ground, godot::Vector3 &position, godot::Quaternion &rotation) const;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	// Applies one Entity State PDU (called for every PDU from the network). `now` < 0 means the current time.
	void handle_entity_state(const godot::Ref<DisEntityStatePdu> &pdu, double now = -1.0);
	// The same with `now` taken as it is, also when negative (DisPlayer restores entities whose last PDU is older
	// than the manager's clock).
	void apply_entity_state(const godot::Ref<DisEntityStatePdu> &pdu, double now);
	// Applies an Entity State Update PDU to a known entity: location, velocity, orientation, appearance and variable
	// parameters change, type, marking and dead reckoning parameters stay those of its last Entity State PDU.
	// Ignored for an entity no Entity State PDU has announced (its type is unknown).
	void handle_entity_state_update(const godot::Ref<DisEntityStateUpdatePdu> &pdu, double now = -1.0);
	// Dead reckons every entity to `now` and removes timed-out ones. Called every frame when auto_update is on.
	void process_entities(double now);
	// Seconds on the manager's clock (what `now` defaults to above).
	double get_clock() const;
	void set_time_scale(double value);
	double get_time_scale() const { return time_scale; }
	void set_max_entities(int value) { max_entities = value < 0 ? 0 : value; }
	int get_max_entities() const { return max_entities; }

	DisRemoteEntity *find_entity(const godot::Vector3i &entity_id) const;
	godot::Object *get_entity(const godot::Vector3i &entity_id) const;
	godot::Array get_entities() const;
	int get_entity_count() const { return static_cast<int>(entities.size()); }
	int64_t get_dropped_count() const { return dropped_count; }
	void remove_entity(const godot::Vector3i &entity_id);
	void clear();

	void set_network_path(const godot::NodePath &value);
	godot::NodePath get_network_path() const { return network_path; }
	void set_origin_path(const godot::NodePath &value) { origin_path = value; }
	godot::NodePath get_origin_path() const { return origin_path; }
	void set_type_map(const godot::Ref<DisEntityTypeMap> &value);
	godot::Ref<DisEntityTypeMap> get_type_map() const;
	void set_timeout(double value) { timeout = value; }
	double get_timeout() const { return timeout; }
	void set_smoothing_time(double value) { smoothing_time = value; }
	double get_smoothing_time() const { return smoothing_time; }
	void set_smoothing_max_distance(double value) { smoothing_max_distance = value; }
	double get_smoothing_max_distance() const { return smoothing_max_distance; }
	void set_dead_reckoning(bool value) { dead_reckoning = value; }
	bool get_dead_reckoning() const { return dead_reckoning; }
	void set_auto_update(bool value);
	bool get_auto_update() const { return auto_update; }
	void set_ground_path(const godot::NodePath &value) { ground_path = value; }
	godot::NodePath get_ground_path() const { return ground_path; }
	void set_ground_clamp(int value) { ground_clamp = value; }
	int get_ground_clamp() const { return ground_clamp; }
	void set_ground_align(bool value) { ground_align = value; }
	bool get_ground_align() const { return ground_align; }
	void set_ground_align_distance(double value) { ground_align_distance = value; }
	double get_ground_align_distance() const { return ground_align_distance; }
	// True if `entity` is of a kind the ground clamp applies to (whether or not a ground node is set).
	bool is_ground_clamped(godot::Object *entity) const;
};

VARIANT_ENUM_CAST(DisEntityManager::GroundClamp);
