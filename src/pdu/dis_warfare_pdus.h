#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include "dis_pdu_base.h"

// Warfare family (protocol family 2): weapon fire and its detonation. Byte layouts: docs/warfare.md.
//
// The fields both PDUs share live here. The event ID ties a Fire to its Detonation; the munition descriptor
// (type, warhead, fuse, quantity, rate) describes what was fired. location_ecef and velocity are in world
// (ECEF) coordinates, metres and metres per second.
class DisWarfarePdu : public DisPduBase {
	GDCLASS(DisWarfarePdu, DisPduBase)

protected:
	godot::Vector3i firing_entity_id;
	godot::Vector3i target_entity_id;
	godot::Vector3i munition_id; // Fire: "munition / expendable ID"; Detonation: "exploding entity ID"
	godot::Vector3i event_id; // site, application, event number
	godot::PackedFloat64Array location_ecef;
	godot::Vector3 velocity;
	godot::PackedInt32Array munition_type;
	int warhead = 0;
	int fuse = 0;
	int quantity = 0;
	int rate = 0;

	static void _bind_methods();

public:
	DisWarfarePdu();

	int get_protocol_family() const override { return 2; }

	void set_firing_entity_id(const godot::Vector3i &value) { firing_entity_id = value; }
	godot::Vector3i get_firing_entity_id() const { return firing_entity_id; }
	void set_target_entity_id(const godot::Vector3i &value) { target_entity_id = value; }
	godot::Vector3i get_target_entity_id() const { return target_entity_id; }
	void set_munition_id(const godot::Vector3i &value) { munition_id = value; }
	godot::Vector3i get_munition_id() const { return munition_id; }
	void set_event_id(const godot::Vector3i &value) { event_id = value; }
	godot::Vector3i get_event_id() const { return event_id; }
	void set_location_ecef(const godot::PackedFloat64Array &value);
	godot::PackedFloat64Array get_location_ecef() const { return location_ecef; }
	void set_velocity(const godot::Vector3 &value) { velocity = value; }
	godot::Vector3 get_velocity() const { return velocity; }
	void set_munition_type(const godot::PackedInt32Array &value);
	godot::PackedInt32Array get_munition_type() const { return munition_type; }
	void set_warhead(int value) { warhead = value; }
	int get_warhead() const { return warhead; }
	void set_fuse(int value) { fuse = value; }
	int get_fuse() const { return fuse; }
	void set_quantity(int value) { quantity = value; }
	int get_quantity() const { return quantity; }
	void set_rate(int value) { rate = value; }
	int get_rate() const { return rate; }
};

// Fire PDU (type 2): a weapon was fired (or an expendable launched) from location_ecef. 96 bytes.
class DisFirePdu : public DisWarfarePdu {
	GDCLASS(DisFirePdu, DisWarfarePdu)

	int64_t fire_mission_index = 0;
	double range = 0.0; // metres, 0 = unknown

protected:
	static void _bind_methods();

public:
	static constexpr int SIZE = 96;

	godot::PackedByteArray encode() const override;
	static godot::Ref<DisFirePdu> decode(const godot::PackedByteArray &bytes);
	int get_pdu_type() const override { return 2; }
	int get_body_length() const override { return SIZE; }

	void set_fire_mission_index(int64_t value) { fire_mission_index = value; }
	int64_t get_fire_mission_index() const { return fire_mission_index; }
	void set_range(double value) { range = value; }
	double get_range() const { return range; }
};

// Detonation PDU (type 3): a munition detonated (or impacted) at location_ecef. 104 bytes + 16 per variable
// parameter. location_in_entity is the point relative to the target entity's body, if it hit one.
class DisDetonationPdu : public DisWarfarePdu {
	GDCLASS(DisDetonationPdu, DisWarfarePdu)

	godot::Vector3 location_in_entity;
	int detonation_result = 0;
	int padding = 0;
	godot::Array variable_parameters; // PackedByteArray of 16 bytes each, kept raw

protected:
	static void _bind_methods();

public:
	static constexpr int BASE_SIZE = 104;
	static constexpr int VARIABLE_PARAMETER_SIZE = 16;

	// The most common SISO-REF-010 "Detonation Result" values (UID 62); DisEnums names all of them.
	enum DetonationResult {
		RESULT_OTHER = 0,
		RESULT_ENTITY_IMPACT = 1,
		RESULT_ENTITY_PROXIMATE_DETONATION = 2,
		RESULT_GROUND_IMPACT = 3,
		RESULT_GROUND_PROXIMATE_DETONATION = 4,
		RESULT_DETONATION = 5,
		RESULT_NONE_OR_DUD = 6,
		RESULT_AIR_HIT = 17,
		RESULT_WATER_IMPACT = 24,
		RESULT_AIR_BURST = 25,
	};

	godot::PackedByteArray encode() const override;
	static godot::Ref<DisDetonationPdu> decode(const godot::PackedByteArray &bytes);
	int get_pdu_type() const override { return 3; }
	int get_body_length() const override;

	void set_location_in_entity(const godot::Vector3 &value) { location_in_entity = value; }
	godot::Vector3 get_location_in_entity() const { return location_in_entity; }
	void set_detonation_result(int value) { detonation_result = value; }
	int get_detonation_result() const { return detonation_result; }
	void set_padding(int value) { padding = value; }
	int get_padding() const { return padding; }
	void set_variable_parameters(const godot::Array &value) { variable_parameters = value; }
	godot::Array get_variable_parameters() const { return variable_parameters; }
};

VARIANT_ENUM_CAST(DisDetonationPdu::DetonationResult);
