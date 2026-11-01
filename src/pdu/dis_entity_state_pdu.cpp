#include "dis_entity_state_pdu.h"

#include <dis7/EntityStatePdu.h>
#include <dis7/utils/DataStream.h>

#include <godot_cpp/core/class_db.hpp>

#include "dis_pdu.h"
#include "pdu_bytes.h"
#include "pdu_convert.h"

using namespace godot;

using namespace pdu_convert;

namespace {
DIS::EntityType to_dis(const PackedInt32Array &type) {
	return to_dis_type(type);
}

DIS::Vector3Float to_dis(const Vector3 &v) {
	return to_dis_vector(v);
}

PackedInt32Array from_dis(const DIS::EntityType &type) {
	return from_dis_type(type);
}

Vector3 from_dis(const DIS::Vector3Float &v) {
	return from_dis_vector(v);
}
} // namespace

DisEntityStatePdu::DisEntityStatePdu() {
	entity_type = zeros(ENTITY_TYPE_FIELDS);
	alternative_entity_type = zeros(ENTITY_TYPE_FIELDS);
	location_ecef.resize(3);
	location_ecef.fill(0.0);
	dr_parameters = resized(PackedByteArray(), DR_PARAMETERS_LENGTH);
	marking_bytes = resized(PackedByteArray(), MARKING_LENGTH);
}

void DisEntityStatePdu::_bind_methods() {
	ClassDB::bind_static_method("DisEntityStatePdu", D_METHOD("decode", "bytes"), &DisEntityStatePdu::decode);

// Binds get_<name>/set_<name> and registers the property, to keep the list below readable.
#define DIS_PROPERTY(variant_type, name)                                                       \
	ClassDB::bind_method(D_METHOD("set_" #name, "value"), &DisEntityStatePdu::set_##name); \
	ClassDB::bind_method(D_METHOD("get_" #name), &DisEntityStatePdu::get_##name);          \
	ADD_PROPERTY(PropertyInfo(Variant::variant_type, #name), "set_" #name, "get_" #name)

	ADD_GROUP("Entity", "");
	DIS_PROPERTY(VECTOR3I, entity_id);
	DIS_PROPERTY(INT, force_id);
	DIS_PROPERTY(PACKED_INT32_ARRAY, entity_type);
	DIS_PROPERTY(PACKED_INT32_ARRAY, alternative_entity_type);
	DIS_PROPERTY(VECTOR3, linear_velocity);
	DIS_PROPERTY(PACKED_FLOAT64_ARRAY, location_ecef);
	DIS_PROPERTY(VECTOR3, orientation);
	DIS_PROPERTY(INT, appearance);
	DIS_PROPERTY(INT, capabilities);
	ADD_GROUP("Dead Reckoning", "dr_");
	DIS_PROPERTY(INT, dr_algorithm);
	DIS_PROPERTY(PACKED_BYTE_ARRAY, dr_parameters);
	DIS_PROPERTY(VECTOR3, dr_linear_acceleration);
	DIS_PROPERTY(VECTOR3, dr_angular_velocity);
	ADD_GROUP("Marking", "");
	DIS_PROPERTY(INT, marking_character_set);
	DIS_PROPERTY(STRING, marking);
	DIS_PROPERTY(PACKED_BYTE_ARRAY, marking_bytes);
	ADD_GROUP("", "");
	DIS_PROPERTY(ARRAY, variable_parameters);
#undef DIS_PROPERTY
}

void DisEntityStatePdu::set_entity_type(const PackedInt32Array &value) {
	ERR_FAIL_COND_MSG(value.size() != ENTITY_TYPE_FIELDS, "entity_type needs 7 values");
	entity_type = value;
}

void DisEntityStatePdu::set_alternative_entity_type(const PackedInt32Array &value) {
	ERR_FAIL_COND_MSG(value.size() != ENTITY_TYPE_FIELDS, "alternative_entity_type needs 7 values");
	alternative_entity_type = value;
}

void DisEntityStatePdu::set_location_ecef(const PackedFloat64Array &value) {
	ERR_FAIL_COND_MSG(value.size() != 3, "location_ecef needs 3 values (x, y, z)");
	location_ecef = value;
}

void DisEntityStatePdu::set_dr_parameters(const PackedByteArray &value) {
	ERR_FAIL_COND_MSG(value.size() > DR_PARAMETERS_LENGTH, "dr_parameters holds at most 15 bytes");
	dr_parameters = resized(value, DR_PARAMETERS_LENGTH);
}

void DisEntityStatePdu::set_marking_bytes(const PackedByteArray &value) {
	ERR_FAIL_COND_MSG(value.size() > MARKING_LENGTH, "marking holds at most 11 bytes");
	marking_bytes = resized(value, MARKING_LENGTH);
}

void DisEntityStatePdu::set_marking(const String &value) {
	PackedByteArray ascii = value.to_ascii_buffer();
	if (ascii.size() > MARKING_LENGTH) {
		ascii.resize(MARKING_LENGTH);
	}
	marking_bytes = resized(ascii, MARKING_LENGTH);
}

String DisEntityStatePdu::get_marking() const {
	// Stops at the first zero byte, like a C string.
	return marking_bytes.get_string_from_ascii();
}

int DisEntityStatePdu::get_length() const {
	return BASE_SIZE + VARIABLE_PARAMETER_SIZE * static_cast<int>(variable_parameters.size());
}

PackedByteArray DisEntityStatePdu::encode() const {
	DIS::EntityStatePdu pdu;
	write_header(pdu);
	pdu.setEntityID(to_dis_entity_id(entity_id));
	pdu.setForceId(static_cast<unsigned char>(force_id));
	pdu.setEntityType(to_dis(entity_type));
	pdu.setAlternativeEntityType(to_dis(alternative_entity_type));
	pdu.setEntityLinearVelocity(to_dis(linear_velocity));

	DIS::Vector3Double location;
	location.setX(location_ecef[0]);
	location.setY(location_ecef[1]);
	location.setZ(location_ecef[2]);
	pdu.setEntityLocation(location);

	DIS::EulerAngles angles;
	angles.setPsi(static_cast<float>(orientation.x));
	angles.setTheta(static_cast<float>(orientation.y));
	angles.setPhi(static_cast<float>(orientation.z));
	pdu.setEntityOrientation(angles);
	pdu.setEntityAppearance(static_cast<unsigned int>(appearance));

	DIS::DeadReckoningParameters dr;
	dr.setDeadReckoningAlgorithm(static_cast<unsigned char>(dr_algorithm));
	unsigned char parameters[DR_PARAMETERS_LENGTH];
	copy_bytes(dr_parameters, parameters, DR_PARAMETERS_LENGTH);
	dr.setParameters(parameters);
	dr.setEntityLinearAcceleration(to_dis(dr_linear_acceleration));
	dr.setEntityAngularVelocity(to_dis(dr_angular_velocity));
	pdu.setDeadReckoningParameters(dr);

	// setCharacters copies all 11 bytes; open-dis-cpp's setByStringCharacters would cut the marking to 10.
	DIS::EntityMarking marking;
	marking.setCharacterSet(static_cast<unsigned char>(marking_character_set));
	unsigned char characters[MARKING_LENGTH];
	copy_bytes(marking_bytes, characters, MARKING_LENGTH);
	marking.setCharacters(reinterpret_cast<const char *>(characters));
	pdu.setMarking(marking);
	pdu.setCapabilities(static_cast<unsigned int>(capabilities));

	std::vector<DIS::VariableParameter> parameters_list;
	for (int64_t i = 0; i < variable_parameters.size(); i++) {
		const PackedByteArray raw = variable_parameters[i];
		ERR_CONTINUE_MSG(raw.size() != VARIABLE_PARAMETER_SIZE, "variable parameters must be 16 bytes each");
		DIS::VariableParameter parameter;
		DIS::DataStream stream = pdu_bytes::to_stream(raw);
		parameter.unmarshal(stream);
		parameters_list.push_back(parameter);
	}
	pdu.setVariableParameters(parameters_list);

	return marshal(pdu);
}

Ref<DisEntityStatePdu> DisEntityStatePdu::decode(const PackedByteArray &bytes) {
	if (!DisPdu::validate(bytes, DisPdu::PDU_TYPE_ENTITY_STATE, BASE_SIZE)) {
		return Ref<DisEntityStatePdu>();
	}
	// Byte 19 is the variable parameter count; make sure they are all inside the packet.
	const int count = bytes[19];
	const int needed = BASE_SIZE + VARIABLE_PARAMETER_SIZE * count;
	if (bytes.size() < needed) {
		DisPdu::set_last_error(vformat("%d variable parameters need %d bytes, got %d", count, needed, bytes.size()));
		return Ref<DisEntityStatePdu>();
	}

	DIS::EntityStatePdu pdu;
	DIS::DataStream stream = pdu_bytes::to_stream(bytes.slice(0, needed));
	pdu.unmarshal(stream);

	Ref<DisEntityStatePdu> out;
	out.instantiate();
	out->read_header(pdu);
	out->entity_id = from_dis_entity_id(pdu.getEntityID());
	out->force_id = pdu.getForceId();
	out->entity_type = from_dis(pdu.getEntityType());
	out->alternative_entity_type = from_dis(pdu.getAlternativeEntityType());
	out->linear_velocity = from_dis(pdu.getEntityLinearVelocity());
	const DIS::Vector3Double &location = pdu.getEntityLocation();
	out->location_ecef.set(0, location.getX());
	out->location_ecef.set(1, location.getY());
	out->location_ecef.set(2, location.getZ());
	const DIS::EulerAngles &angles = pdu.getEntityOrientation();
	out->orientation = Vector3(angles.getPsi(), angles.getTheta(), angles.getPhi());
	out->appearance = pdu.getEntityAppearance();

	const DIS::DeadReckoningParameters &dr = pdu.getDeadReckoningParameters();
	out->dr_algorithm = dr.getDeadReckoningAlgorithm();
	for (int i = 0; i < DR_PARAMETERS_LENGTH; i++) {
		out->dr_parameters.set(i, dr.getParameters()[i]);
	}
	out->dr_linear_acceleration = from_dis(dr.getEntityLinearAcceleration());
	out->dr_angular_velocity = from_dis(dr.getEntityAngularVelocity());

	const DIS::EntityMarking &marking = pdu.getMarking();
	out->marking_character_set = marking.getCharacterSet();
	for (int i = 0; i < MARKING_LENGTH; i++) {
		out->marking_bytes.set(i, static_cast<uint8_t>(marking.getCharacters()[i]));
	}
	out->capabilities = pdu.getCapabilities();

	// Keep variable parameters as raw 16-byte records so they round-trip exactly.
	for (int i = 0; i < count; i++) {
		const int64_t start = BASE_SIZE + VARIABLE_PARAMETER_SIZE * i;
		out->variable_parameters.push_back(bytes.slice(start, start + VARIABLE_PARAMETER_SIZE));
	}
	return out;
}
