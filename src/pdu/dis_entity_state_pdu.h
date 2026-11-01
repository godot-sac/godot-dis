#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include "dis_pdu_base.h"

// DIS 7 Entity State PDU (type 1) as a Godot object. Fields are plain Godot values; encode() and
// decode() go through open-dis-cpp. Byte layout: docs/entity-state-pdu.md.
//
// Units: metres, metres per second, radians. Location is WGS84 geocentric (ECEF) and kept in doubles
// (PackedFloat64Array) because Vector3 is 32-bit.
class DisEntityStatePdu : public DisPduBase {
	GDCLASS(DisEntityStatePdu, DisPduBase)

public:
	static constexpr int BASE_SIZE = 144;
	static constexpr int VARIABLE_PARAMETER_SIZE = 16;
	static constexpr int MARKING_LENGTH = 11;
	static constexpr int DR_PARAMETERS_LENGTH = 15;

private:
	godot::Vector3i entity_id; // x = site, y = application, z = entity number
	int force_id = 0;
	godot::PackedInt32Array entity_type; // kind, domain, country, category, subcategory, specific, extra
	godot::PackedInt32Array alternative_entity_type;
	godot::Vector3 linear_velocity;
	godot::PackedFloat64Array location_ecef;
	godot::Vector3 orientation; // x = psi, y = theta, z = phi
	int64_t appearance = 0;
	int dr_algorithm = 0;
	godot::PackedByteArray dr_parameters;
	godot::Vector3 dr_linear_acceleration;
	godot::Vector3 dr_angular_velocity;
	int marking_character_set = 1;
	godot::PackedByteArray marking_bytes;
	int64_t capabilities = 0;
	godot::Array variable_parameters; // PackedByteArray of 16 bytes each, kept raw

protected:
	static void _bind_methods();

public:
	DisEntityStatePdu();

	godot::PackedByteArray encode() const override;
	// Parses DIS bytes. Returns null on malformed input; see DisPdu.get_last_error().
	static godot::Ref<DisEntityStatePdu> decode(const godot::PackedByteArray &bytes);
	int get_pdu_type() const override { return 1; }
	int get_protocol_family() const override { return 1; }
	// Encoded size: 144 + 16 per variable parameter.
	int get_length() const override;

	// Getters and setters (exposed as properties).
	void set_entity_id(const godot::Vector3i &value) { entity_id = value; }
	godot::Vector3i get_entity_id() const { return entity_id; }
	void set_force_id(int value) { force_id = value; }
	int get_force_id() const { return force_id; }
	void set_entity_type(const godot::PackedInt32Array &value);
	godot::PackedInt32Array get_entity_type() const { return entity_type; }
	void set_alternative_entity_type(const godot::PackedInt32Array &value);
	godot::PackedInt32Array get_alternative_entity_type() const { return alternative_entity_type; }
	void set_linear_velocity(const godot::Vector3 &value) { linear_velocity = value; }
	godot::Vector3 get_linear_velocity() const { return linear_velocity; }
	void set_location_ecef(const godot::PackedFloat64Array &value);
	godot::PackedFloat64Array get_location_ecef() const { return location_ecef; }
	void set_orientation(const godot::Vector3 &value) { orientation = value; }
	godot::Vector3 get_orientation() const { return orientation; }
	void set_appearance(int64_t value) { appearance = value; }
	int64_t get_appearance() const { return appearance; }
	void set_dr_algorithm(int value) { dr_algorithm = value; }
	int get_dr_algorithm() const { return dr_algorithm; }
	void set_dr_parameters(const godot::PackedByteArray &value);
	godot::PackedByteArray get_dr_parameters() const { return dr_parameters; }
	void set_dr_linear_acceleration(const godot::Vector3 &value) { dr_linear_acceleration = value; }
	godot::Vector3 get_dr_linear_acceleration() const { return dr_linear_acceleration; }
	void set_dr_angular_velocity(const godot::Vector3 &value) { dr_angular_velocity = value; }
	godot::Vector3 get_dr_angular_velocity() const { return dr_angular_velocity; }
	void set_marking_character_set(int value) { marking_character_set = value; }
	int get_marking_character_set() const { return marking_character_set; }
	void set_marking_bytes(const godot::PackedByteArray &value);
	godot::PackedByteArray get_marking_bytes() const { return marking_bytes; }
	void set_marking(const godot::String &value);
	godot::String get_marking() const;
	void set_capabilities(int64_t value) { capabilities = value; }
	int64_t get_capabilities() const { return capabilities; }
	void set_variable_parameters(const godot::Array &value) { variable_parameters = value; }
	godot::Array get_variable_parameters() const { return variable_parameters; }
};
