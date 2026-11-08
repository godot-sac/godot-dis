#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include "dis_pdu_base.h"

// Entity State Update PDU (type 67, Entity Information family): the changing part of an Entity State PDU
// (velocity, location, orientation, appearance, variable parameters) for an entity whose type, marking and dead
// reckoning parameters are already known from an earlier Entity State PDU. 72 bytes plus 16 per variable parameter.
// Encoded with our own code (byte_io.h).
class DisEntityStateUpdatePdu : public DisPduBase {
	GDCLASS(DisEntityStateUpdatePdu, DisPduBase)

	godot::Vector3i entity_id;
	godot::Vector3 linear_velocity; // m/s, ECEF axes
	godot::PackedFloat64Array location_ecef; // m
	godot::Vector3 orientation; // psi, theta, phi in radians
	int64_t appearance = 0;
	godot::Array variable_parameters; // PackedByteArray of 16 bytes each, kept raw

protected:
	static void _bind_methods();

public:
	static constexpr int BASE_SIZE = 72;
	static constexpr int VARIABLE_PARAMETER_SIZE = 16;

	DisEntityStateUpdatePdu();

	godot::PackedByteArray encode() const override;
	static godot::Ref<DisEntityStateUpdatePdu> decode(const godot::PackedByteArray &bytes);
	int get_pdu_type() const override { return 67; }
	int get_protocol_family() const override { return 1; }
	int get_body_length() const override;

	void set_entity_id(const godot::Vector3i &value) { entity_id = value; }
	godot::Vector3i get_entity_id() const { return entity_id; }
	void set_linear_velocity(const godot::Vector3 &value) { linear_velocity = value; }
	godot::Vector3 get_linear_velocity() const { return linear_velocity; }
	void set_location_ecef(const godot::PackedFloat64Array &value);
	godot::PackedFloat64Array get_location_ecef() const { return location_ecef; }
	void set_orientation(const godot::Vector3 &value) { orientation = value; }
	godot::Vector3 get_orientation() const { return orientation; }
	void set_appearance(int64_t value) { appearance = value; }
	int64_t get_appearance() const { return appearance; }
	void set_variable_parameters(const godot::Array &value) { variable_parameters = value; }
	godot::Array get_variable_parameters() const { return variable_parameters; }
};
