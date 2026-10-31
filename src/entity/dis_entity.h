#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include "entity/dead_reckoning.h"

class DisEntityStatePdu;
class DisGeoOrigin;
class DisNetwork;

// A local entity that publishes itself: put it on (or above) a moving node and it sends Entity State PDUs
// for its global transform. Like real DIS simulations it does not send every frame: it runs the same dead
// reckoning the receivers run on what it last sent, and sends again when that prediction is off by more than
// position_threshold / orientation_threshold, or when heartbeat_interval passes.
class DisEntity : public godot::Node3D {
	GDCLASS(DisEntity, godot::Node3D)

public:
	enum VelocitySource {
		VELOCITY_ESTIMATED, // from the node's motion between physics frames
		VELOCITY_MANUAL, // set linear_velocity / linear_acceleration / angular_velocity yourself
	};
	static constexpr int64_t APPEARANCE_DEACTIVATED = 1 << 23; // general appearance "State" bit

private:
	godot::NodePath network_path;
	godot::NodePath origin_path;
	godot::Vector3i entity_id; // 0 fields = site / application from the network, entity number assigned
	int force_id = 1;
	godot::PackedInt32Array entity_type;
	godot::String marking;
	int64_t appearance = 0;
	int64_t capabilities = 0;
	int dr_algorithm = dis::dr::ALGORITHM_FPW;
	double heartbeat_interval = 5.0;
	double position_threshold = 1.0;
	double orientation_threshold = 0.05235987755982988; // 3 degrees
	bool publishing = true;
	bool auto_update = true;
	int velocity_source = VELOCITY_ESTIMATED;
	// Godot space: velocity / acceleration in global axes, angular velocity in the node's own axes (rad/s).
	godot::Vector3 linear_velocity;
	godot::Vector3 linear_acceleration;
	godot::Vector3 angular_velocity;

	// Motion estimate
	bool has_sample = false;
	double sample_time = 0.0;
	godot::Vector3 sample_position;
	godot::Basis sample_basis;

	// What receivers know about us
	bool has_sent = false;
	bool dirty = true;
	double sent_time = 0.0;
	dis::dr::State sent_state;
	int64_t published_count = 0;
	int assigned_number = 0;
	bool warned_no_origin = false;

	godot::ObjectID network_id;

	DisNetwork *get_network();
	DisGeoOrigin *get_origin();
	void sample_motion(double now);
	dis::dr::State current_state(DisGeoOrigin *origin) const;
	godot::Ref<DisEntityStatePdu> make_pdu(const dis::dr::State &state, int64_t pdu_appearance);
	godot::Error send(const dis::dr::State &state, double now, int64_t pdu_appearance);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	DisEntity();

	// Samples the motion and sends a PDU if needed. Called every physics frame when auto_update is on;
	// `now` is in seconds (Time.get_ticks_usec() / 1e6).
	void process_entity(double now);
	// Sends a PDU now, regardless of thresholds.
	godot::Error send_now();
	// The PDU this entity would send now (not sent).
	godot::Ref<DisEntityStatePdu> create_pdu();
	godot::Vector3i get_effective_entity_id();
	int64_t get_published_count() const { return published_count; }

	void set_network_path(const godot::NodePath &value);
	godot::NodePath get_network_path() const { return network_path; }
	void set_origin_path(const godot::NodePath &value) { origin_path = value; }
	godot::NodePath get_origin_path() const { return origin_path; }
	void set_entity_id(const godot::Vector3i &value);
	godot::Vector3i get_entity_id() const { return entity_id; }
	void set_force_id(int value);
	int get_force_id() const { return force_id; }
	void set_entity_type(const godot::PackedInt32Array &value);
	godot::PackedInt32Array get_entity_type() const { return entity_type; }
	void set_marking(const godot::String &value);
	godot::String get_marking() const { return marking; }
	void set_appearance(int64_t value);
	int64_t get_appearance() const { return appearance; }
	void set_capabilities(int64_t value);
	int64_t get_capabilities() const { return capabilities; }
	void set_dr_algorithm(int value);
	int get_dr_algorithm() const { return dr_algorithm; }
	void set_heartbeat_interval(double value) { heartbeat_interval = value; }
	double get_heartbeat_interval() const { return heartbeat_interval; }
	void set_position_threshold(double value) { position_threshold = value; }
	double get_position_threshold() const { return position_threshold; }
	void set_orientation_threshold(double value) { orientation_threshold = value; }
	double get_orientation_threshold() const { return orientation_threshold; }
	void set_publishing(bool value) { publishing = value; }
	bool is_publishing() const { return publishing; }
	void set_auto_update(bool value);
	bool get_auto_update() const { return auto_update; }
	void set_velocity_source(int value) { velocity_source = value; }
	int get_velocity_source() const { return velocity_source; }
	void set_linear_velocity(const godot::Vector3 &value) { linear_velocity = value; }
	godot::Vector3 get_linear_velocity() const { return linear_velocity; }
	void set_linear_acceleration(const godot::Vector3 &value) { linear_acceleration = value; }
	godot::Vector3 get_linear_acceleration() const { return linear_acceleration; }
	void set_angular_velocity(const godot::Vector3 &value) { angular_velocity = value; }
	godot::Vector3 get_angular_velocity() const { return angular_velocity; }
};

VARIANT_ENUM_CAST(DisEntity::VelocitySource);
