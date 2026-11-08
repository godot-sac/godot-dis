#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include "dis_pdu_base.h"

// Distributed Emission Regeneration family (protocol family 6): sensors and emitters. Electromagnetic Emission
// (radars, jammers), Designator (laser designators) and IFF. Encoded with our own code (byte_io.h); godot-dis
// decodes and sends them, it does not model what the emissions detect. Layouts: docs/emissions.md.

// Electromagnetic Emission PDU (type 23): the emitter systems of one entity, their beams and what each beam tracks
// or jams. 28 bytes plus 20 per system, 52 per beam and 8 per tracked or jammed target.
// Systems and beams are Dictionaries, see docs/emissions.md for their keys.
class DisElectromagneticEmissionPdu : public DisPduBase {
	GDCLASS(DisElectromagneticEmissionPdu, DisPduBase)

	godot::Vector3i emitting_entity_id;
	godot::Vector3i event_id; // site, application, event number
	int state_update_indicator = 0;
	godot::Array systems;

protected:
	static void _bind_methods();

public:
	static constexpr int BASE_SIZE = 28;
	static constexpr int SYSTEM_SIZE = 20;
	static constexpr int BEAM_SIZE = 52;
	static constexpr int TRACK_JAM_SIZE = 8;

	// SISO-REF-010 "EEPDU State Update Indicator" (UID 77).
	enum StateUpdateIndicator {
		STATE_HEARTBEAT = 0,
		STATE_CHANGED_DATA = 1,
	};

	godot::PackedByteArray encode() const override;
	static godot::Ref<DisElectromagneticEmissionPdu> decode(const godot::PackedByteArray &bytes);
	int get_pdu_type() const override { return 23; }
	int get_protocol_family() const override { return 6; }
	int get_body_length() const override;

	// A system Dictionary with every key set to its default and no beams.
	static godot::Dictionary make_system();
	// A beam Dictionary with every key set to its default and no targets.
	static godot::Dictionary make_beam();

	void set_emitting_entity_id(const godot::Vector3i &value) { emitting_entity_id = value; }
	godot::Vector3i get_emitting_entity_id() const { return emitting_entity_id; }
	void set_event_id(const godot::Vector3i &value) { event_id = value; }
	godot::Vector3i get_event_id() const { return event_id; }
	void set_state_update_indicator(int value) { state_update_indicator = value; }
	int get_state_update_indicator() const { return state_update_indicator; }
	void set_systems(const godot::Array &value) { systems = value; }
	godot::Array get_systems() const { return systems; }
};

// Designator PDU (type 24): a laser designator spot. 88 bytes.
class DisDesignatorPdu : public DisPduBase {
	GDCLASS(DisDesignatorPdu, DisPduBase)

	godot::Vector3i designating_entity_id;
	int code_name = 0;
	godot::Vector3i designated_entity_id;
	int designator_code = 0;
	double power = 0.0; // W
	double wavelength = 0.0; // micrometres
	godot::Vector3 spot_offset; // m, from the designated entity, in its body axes
	godot::PackedFloat64Array spot_location_ecef; // m
	int dr_algorithm = 0;
	godot::Vector3 spot_linear_acceleration; // m/s^2

protected:
	static void _bind_methods();

public:
	static constexpr int BASE_SIZE = 88;

	DisDesignatorPdu();

	godot::PackedByteArray encode() const override;
	static godot::Ref<DisDesignatorPdu> decode(const godot::PackedByteArray &bytes);
	int get_pdu_type() const override { return 24; }
	int get_protocol_family() const override { return 6; }
	int get_body_length() const override { return BASE_SIZE; }

	void set_designating_entity_id(const godot::Vector3i &value) { designating_entity_id = value; }
	godot::Vector3i get_designating_entity_id() const { return designating_entity_id; }
	void set_code_name(int value) { code_name = value; }
	int get_code_name() const { return code_name; }
	void set_designated_entity_id(const godot::Vector3i &value) { designated_entity_id = value; }
	godot::Vector3i get_designated_entity_id() const { return designated_entity_id; }
	void set_designator_code(int value) { designator_code = value; }
	int get_designator_code() const { return designator_code; }
	void set_power(double value) { power = value; }
	double get_power() const { return power; }
	void set_wavelength(double value) { wavelength = value; }
	double get_wavelength() const { return wavelength; }
	void set_spot_offset(const godot::Vector3 &value) { spot_offset = value; }
	godot::Vector3 get_spot_offset() const { return spot_offset; }
	void set_spot_location_ecef(const godot::PackedFloat64Array &value);
	godot::PackedFloat64Array get_spot_location_ecef() const { return spot_location_ecef; }
	void set_dr_algorithm(int value) { dr_algorithm = value; }
	int get_dr_algorithm() const { return dr_algorithm; }
	void set_spot_linear_acceleration(const godot::Vector3 &value) { spot_linear_acceleration = value; }
	godot::Vector3 get_spot_linear_acceleration() const { return spot_linear_acceleration; }
};

// IFF PDU (type 28): an IFF / SSR transponder or interrogator. Layer 1 (60 bytes) is decoded into properties;
// further layers (Mode 5, Mode S, ...) are kept as raw records.
class DisIffPdu : public DisPduBase {
	GDCLASS(DisIffPdu, DisPduBase)

	godot::Vector3i emitting_entity_id;
	godot::Vector3i event_id; // site, application, event number
	godot::Vector3 relative_antenna_location; // m, entity body axes
	int system_type = 0;
	int system_name = 0;
	int system_mode = 0;
	int change_options = 0;
	int system_designator = 0;
	int system_specific_data = 0;
	int system_status = 0;
	int data_field_1 = 0; // alternate parameter 4
	int information_layers = 0;
	int data_field_2 = 0; // modifier
	godot::PackedInt32Array parameters; // parameters 1-6: Mode 1, Mode 2, Mode 3/A, Mode 4, Mode C, Mode 5 / S
	godot::Array layers; // PackedByteArray per further layer, its 4-byte layer header included

protected:
	static void _bind_methods();

public:
	static constexpr int BASE_SIZE = 60;
	static constexpr int LAYER_HEADER_SIZE = 4;

	DisIffPdu();

	godot::PackedByteArray encode() const override;
	static godot::Ref<DisIffPdu> decode(const godot::PackedByteArray &bytes);
	int get_pdu_type() const override { return 28; }
	int get_protocol_family() const override { return 6; }
	int get_body_length() const override;

	void set_emitting_entity_id(const godot::Vector3i &value) { emitting_entity_id = value; }
	godot::Vector3i get_emitting_entity_id() const { return emitting_entity_id; }
	void set_event_id(const godot::Vector3i &value) { event_id = value; }
	godot::Vector3i get_event_id() const { return event_id; }
	void set_relative_antenna_location(const godot::Vector3 &value) { relative_antenna_location = value; }
	godot::Vector3 get_relative_antenna_location() const { return relative_antenna_location; }
	void set_system_type(int value) { system_type = value; }
	int get_system_type() const { return system_type; }
	void set_system_name(int value) { system_name = value; }
	int get_system_name() const { return system_name; }
	void set_system_mode(int value) { system_mode = value; }
	int get_system_mode() const { return system_mode; }
	void set_change_options(int value) { change_options = value; }
	int get_change_options() const { return change_options; }
	void set_system_designator(int value) { system_designator = value; }
	int get_system_designator() const { return system_designator; }
	void set_system_specific_data(int value) { system_specific_data = value; }
	int get_system_specific_data() const { return system_specific_data; }
	void set_system_status(int value) { system_status = value; }
	int get_system_status() const { return system_status; }
	void set_data_field_1(int value) { data_field_1 = value; }
	int get_data_field_1() const { return data_field_1; }
	void set_information_layers(int value) { information_layers = value; }
	int get_information_layers() const { return information_layers; }
	void set_data_field_2(int value) { data_field_2 = value; }
	int get_data_field_2() const { return data_field_2; }
	void set_parameters(const godot::PackedInt32Array &value);
	godot::PackedInt32Array get_parameters() const { return parameters; }
	void set_layers(const godot::Array &value) { layers = value; }
	godot::Array get_layers() const { return layers; }
};

VARIANT_ENUM_CAST(DisElectromagneticEmissionPdu::StateUpdateIndicator);
