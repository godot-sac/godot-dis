#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

// Typed variable parameter records of the Entity State, Entity State Update and Detonation PDUs (16 bytes each,
// IEEE 1278.1-2012 6.2.94): articulated parts (a turret's azimuth, a gun's elevation, a rotor's rotation) and attached
// parts (stores on a station). Static helpers that build the raw records the PDUs carry and read them back:
//     pdu.variable_parameters = [DisVariableParameter.articulated_part(4096, DisVariableParameter.METRIC_AZIMUTH, 0.5)]
// Type classes, metrics and stations are SISO-REF-010 values (DisEnums tables articulated_part_class,
// articulated_part_metric, attached_part). Layouts: docs/entities.md.
class DisVariableParameter : public godot::Object {
	GDCLASS(DisVariableParameter, godot::Object)

protected:
	static void _bind_methods();

public:
	static constexpr int SIZE = 16;

	enum RecordType {
		RECORD_ARTICULATED_PART = 0,
		RECORD_ATTACHED_PART = 1,
		RECORD_SEPARATION = 2,
		RECORD_ENTITY_TYPE = 3,
		RECORD_ENTITY_ASSOCIATION = 4,
	};

	// Articulated part type metrics (SISO-REF-010 UID 58); the parameter type is type class + metric.
	enum Metric {
		METRIC_NOT_SPECIFIED = 0,
		METRIC_POSITION = 1,
		METRIC_POSITION_RATE = 2,
		METRIC_EXTENSION = 3,
		METRIC_EXTENSION_RATE = 4,
		METRIC_X = 5,
		METRIC_X_RATE = 6,
		METRIC_Y = 7,
		METRIC_Y_RATE = 8,
		METRIC_Z = 9,
		METRIC_Z_RATE = 10,
		METRIC_AZIMUTH = 11,
		METRIC_AZIMUTH_RATE = 12,
		METRIC_ELEVATION = 13,
		METRIC_ELEVATION_RATE = 14,
		METRIC_ROTATION = 15,
		METRIC_ROTATION_RATE = 16,
	};

	// Articulated part record: `type_class` a multiple of 32 (e.g. 4096 primary turret 1, 4416 primary gun 1),
	// `metric` 0..31, `value` (radians, metres or their rates; 32-bit float), `attached_to` the part ID this one is
	// attached to (0 = the entity), `change` the change indicator (count it up when the part changes).
	static godot::PackedByteArray articulated_part(int64_t type_class, int64_t metric, double value,
			int64_t attached_to = 0, int64_t change = 0);
	// Attached part record: `station` (SISO attached part, e.g. 512 + n fuselage stations), `entity_type` of the
	// part (7 values as DisEntityStatePdu.entity_type), `attached_to` part ID, `detached` indicator.
	static godot::PackedByteArray attached_part(int64_t station, const godot::PackedInt32Array &entity_type,
			int64_t attached_to = 0, bool detached = false);
	// A record as a Dictionary: always record_type; for articulated parts change, attached_to, parameter_type,
	// type_class, metric, value; for attached parts detached, attached_to, station, entity_type; other record
	// types keep their 15 bytes after the type in `data`. An empty Dictionary (and an error) if not 16 bytes.
	static godot::Dictionary decode(const godot::PackedByteArray &record);
	// decode() of every record of a PDU's variable_parameters.
	static godot::Array decode_all(const godot::Array &records);
};

VARIANT_ENUM_CAST(DisVariableParameter::RecordType);
VARIANT_ENUM_CAST(DisVariableParameter::Metric);
