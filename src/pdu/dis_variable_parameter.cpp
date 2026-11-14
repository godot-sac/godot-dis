#include "dis_variable_parameter.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "pdu/byte_io.h"

using namespace godot;

void DisVariableParameter::_bind_methods() {
	ClassDB::bind_static_method("DisVariableParameter",
			D_METHOD("articulated_part", "type_class", "metric", "value", "attached_to", "change"),
			&DisVariableParameter::articulated_part, DEFVAL(0), DEFVAL(0));
	ClassDB::bind_static_method("DisVariableParameter",
			D_METHOD("attached_part", "station", "entity_type", "attached_to", "detached"),
			&DisVariableParameter::attached_part, DEFVAL(0), DEFVAL(false));
	ClassDB::bind_static_method("DisVariableParameter", D_METHOD("decode", "record"), &DisVariableParameter::decode);
	ClassDB::bind_static_method("DisVariableParameter", D_METHOD("decode_all", "records"),
			&DisVariableParameter::decode_all);
	BIND_ENUM_CONSTANT(RECORD_ARTICULATED_PART);
	BIND_ENUM_CONSTANT(RECORD_ATTACHED_PART);
	BIND_ENUM_CONSTANT(RECORD_SEPARATION);
	BIND_ENUM_CONSTANT(RECORD_ENTITY_TYPE);
	BIND_ENUM_CONSTANT(RECORD_ENTITY_ASSOCIATION);
	BIND_ENUM_CONSTANT(METRIC_NOT_SPECIFIED);
	BIND_ENUM_CONSTANT(METRIC_POSITION);
	BIND_ENUM_CONSTANT(METRIC_POSITION_RATE);
	BIND_ENUM_CONSTANT(METRIC_EXTENSION);
	BIND_ENUM_CONSTANT(METRIC_EXTENSION_RATE);
	BIND_ENUM_CONSTANT(METRIC_X);
	BIND_ENUM_CONSTANT(METRIC_X_RATE);
	BIND_ENUM_CONSTANT(METRIC_Y);
	BIND_ENUM_CONSTANT(METRIC_Y_RATE);
	BIND_ENUM_CONSTANT(METRIC_Z);
	BIND_ENUM_CONSTANT(METRIC_Z_RATE);
	BIND_ENUM_CONSTANT(METRIC_AZIMUTH);
	BIND_ENUM_CONSTANT(METRIC_AZIMUTH_RATE);
	BIND_ENUM_CONSTANT(METRIC_ELEVATION);
	BIND_ENUM_CONSTANT(METRIC_ELEVATION_RATE);
	BIND_ENUM_CONSTANT(METRIC_ROTATION);
	BIND_ENUM_CONSTANT(METRIC_ROTATION_RATE);
}

PackedByteArray DisVariableParameter::articulated_part(int64_t type_class, int64_t metric, double value,
		int64_t attached_to, int64_t change) {
	ERR_FAIL_COND_V_MSG(metric < 0 || metric > 31, PackedByteArray(), vformat("metric %d is not 0..31", metric));
	ERR_FAIL_COND_V_MSG(type_class < 0 || type_class % 32 != 0 || type_class + metric > 0xFFFFFFFFLL,
			PackedByteArray(), vformat("type class %d is not a multiple of 32", type_class));
	byte_io::Writer out;
	out.u8(RECORD_ARTICULATED_PART);
	out.u8(static_cast<uint64_t>(change) & 0xFF);
	out.u16(static_cast<uint64_t>(attached_to) & 0xFFFF);
	out.u32(static_cast<uint64_t>(type_class + metric));
	out.f32(value);
	out.u32(0); // padding
	return out.data();
}

PackedByteArray DisVariableParameter::attached_part(int64_t station, const PackedInt32Array &entity_type,
		int64_t attached_to, bool detached) {
	ERR_FAIL_COND_V_MSG(entity_type.size() != 7, PackedByteArray(),
			"entity_type needs 7 values (kind, domain, country, category, subcategory, specific, extra)");
	byte_io::Writer out;
	out.u8(RECORD_ATTACHED_PART);
	out.u8(detached ? 1 : 0);
	out.u16(static_cast<uint64_t>(attached_to) & 0xFFFF);
	out.u32(static_cast<uint64_t>(station) & 0xFFFFFFFF);
	out.u8(entity_type[0]);
	out.u8(entity_type[1]);
	out.u16(entity_type[2]);
	for (int i = 3; i < 7; i++) {
		out.u8(entity_type[i]);
	}
	return out.data();
}

Dictionary DisVariableParameter::decode(const PackedByteArray &record) {
	Dictionary result;
	ERR_FAIL_COND_V_MSG(record.size() != SIZE, result, vformat("a variable parameter is 16 bytes, not %d", record.size()));
	byte_io::Reader in(record, 0, SIZE);
	const int64_t type = static_cast<int64_t>(in.u8());
	result["record_type"] = type;
	if (type == RECORD_ARTICULATED_PART) {
		result["change"] = static_cast<int64_t>(in.u8());
		result["attached_to"] = static_cast<int64_t>(in.u16());
		const int64_t parameter_type = static_cast<int64_t>(in.u32());
		result["parameter_type"] = parameter_type;
		result["type_class"] = parameter_type & ~int64_t(31);
		result["metric"] = parameter_type & 31;
		result["value"] = in.f32();
	} else if (type == RECORD_ATTACHED_PART) {
		result["detached"] = in.u8() != 0;
		result["attached_to"] = static_cast<int64_t>(in.u16());
		result["station"] = static_cast<int64_t>(in.u32());
		PackedInt32Array entity_type;
		entity_type.push_back(int32_t(in.u8()));
		entity_type.push_back(int32_t(in.u8()));
		entity_type.push_back(int32_t(in.u16()));
		for (int i = 3; i < 7; i++) {
			entity_type.push_back(int32_t(in.u8()));
		}
		result["entity_type"] = entity_type;
	} else {
		result["data"] = in.raw(SIZE - 1);
	}
	return result;
}

Array DisVariableParameter::decode_all(const Array &records) {
	Array out;
	for (int64_t i = 0; i < records.size(); i++) {
		ERR_CONTINUE_MSG(records[i].get_type() != Variant::PACKED_BYTE_ARRAY,
				vformat("variable parameter %d is not a PackedByteArray", i));
		out.push_back(decode(records[i]));
	}
	return out;
}
