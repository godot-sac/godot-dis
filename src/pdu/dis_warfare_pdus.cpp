#include "dis_warfare_pdus.h"

#include <dis7/DetonationPdu.h>
#include <dis7/FirePdu.h>
#include <dis7/MunitionDescriptor.h>

#include <godot_cpp/core/class_db.hpp>

#include "dis_pdu.h"
#include "pdu_bytes.h"
#include "pdu_convert.h"

using namespace godot;
using namespace pdu_convert;

// ---------------------------------------------------------------------------------------------------------------
// Shared fields

DisWarfarePdu::DisWarfarePdu() {
	location_ecef = zero_location();
	munition_type = zeros(ENTITY_TYPE_FIELDS);
}

void DisWarfarePdu::_bind_methods() {
	ADD_GROUP("Warfare", "");
	DIS_BIND_PROPERTY(DisWarfarePdu, VECTOR3I, firing_entity_id);
	DIS_BIND_PROPERTY(DisWarfarePdu, VECTOR3I, target_entity_id);
	DIS_BIND_PROPERTY(DisWarfarePdu, VECTOR3I, munition_id);
	DIS_BIND_PROPERTY(DisWarfarePdu, VECTOR3I, event_id);
	DIS_BIND_PROPERTY(DisWarfarePdu, PACKED_FLOAT64_ARRAY, location_ecef);
	DIS_BIND_PROPERTY(DisWarfarePdu, VECTOR3, velocity);
	ADD_GROUP("Munition Descriptor", "");
	DIS_BIND_PROPERTY(DisWarfarePdu, PACKED_INT32_ARRAY, munition_type);
	DIS_BIND_PROPERTY(DisWarfarePdu, INT, warhead);
	DIS_BIND_PROPERTY(DisWarfarePdu, INT, fuse);
	DIS_BIND_PROPERTY(DisWarfarePdu, INT, quantity);
	DIS_BIND_PROPERTY(DisWarfarePdu, INT, rate);
	ADD_GROUP("", "");
}

void DisWarfarePdu::set_location_ecef(const PackedFloat64Array &value) {
	ERR_FAIL_COND_MSG(value.size() != 3, "location_ecef needs 3 values (x, y, z)");
	location_ecef = value;
}

void DisWarfarePdu::set_munition_type(const PackedInt32Array &value) {
	ERR_FAIL_COND_MSG(value.size() != ENTITY_TYPE_FIELDS, "munition_type needs 7 values");
	munition_type = value;
}

namespace {
DIS::MunitionDescriptor to_dis_descriptor(const PackedInt32Array &type, int warhead, int fuse, int quantity, int rate) {
	DIS::MunitionDescriptor out;
	out.setMunitionType(to_dis_type(type));
	out.setWarhead(static_cast<unsigned short>(warhead));
	out.setFuse(static_cast<unsigned short>(fuse));
	out.setQuantity(static_cast<unsigned short>(quantity));
	out.setRate(static_cast<unsigned short>(rate));
	return out;
}
} // namespace

// Reads the descriptor into the shared fields (a macro because the fields are protected members of `out`).
#define DIS_READ_DESCRIPTOR(out, descriptor)                                \
	(out)->munition_type = from_dis_type((descriptor).getMunitionType()); \
	(out)->warhead = (descriptor).getWarhead();                           \
	(out)->fuse = (descriptor).getFuse();                                 \
	(out)->quantity = (descriptor).getQuantity();                         \
	(out)->rate = (descriptor).getRate()

// ---------------------------------------------------------------------------------------------------------------
// Fire

void DisFirePdu::_bind_methods() {
	ClassDB::bind_static_method("DisFirePdu", D_METHOD("decode", "bytes"), &DisFirePdu::decode);
	ADD_GROUP("Fire", "");
	DIS_BIND_PROPERTY(DisFirePdu, INT, fire_mission_index);
	DIS_BIND_PROPERTY(DisFirePdu, FLOAT, range);
	ADD_GROUP("", "");
}

PackedByteArray DisFirePdu::encode() const {
	DIS::FirePdu pdu;
	write_header(pdu);
	pdu.setFiringEntityID(to_dis_entity_id(firing_entity_id));
	pdu.setTargetEntityID(to_dis_entity_id(target_entity_id));
	pdu.setMunitionExpendibleID(to_dis_entity_id(munition_id));
	pdu.setEventID(to_dis_event_id(event_id));
	pdu.setFireMissionIndex(static_cast<unsigned int>(fire_mission_index));
	pdu.setLocationInWorldCoordinates(to_dis_location(location_ecef));
	pdu.setDescriptor(to_dis_descriptor(munition_type, warhead, fuse, quantity, rate));
	pdu.setVelocity(to_dis_vector(velocity));
	pdu.setRange(static_cast<float>(range));
	return marshal(pdu);
}

Ref<DisFirePdu> DisFirePdu::decode(const PackedByteArray &bytes) {
	if (!DisPdu::validate(bytes, DisPdu::PDU_TYPE_FIRE, SIZE)) {
		return Ref<DisFirePdu>();
	}
	DIS::FirePdu pdu;
	DIS::DataStream stream = pdu_bytes::to_stream(bytes.slice(0, SIZE));
	pdu.unmarshal(stream);

	Ref<DisFirePdu> out;
	out.instantiate();
	out->read_header(pdu);
	out->firing_entity_id = from_dis_entity_id(pdu.getFiringEntityID());
	out->target_entity_id = from_dis_entity_id(pdu.getTargetEntityID());
	out->munition_id = from_dis_entity_id(pdu.getMunitionExpendibleID());
	out->event_id = from_dis_event_id(pdu.getEventID());
	out->fire_mission_index = pdu.getFireMissionIndex();
	out->location_ecef = from_dis_location(pdu.getLocationInWorldCoordinates());
	DIS_READ_DESCRIPTOR(out, pdu.getDescriptor());
	out->velocity = from_dis_vector(pdu.getVelocity());
	out->range = pdu.getRange();
	return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Detonation

void DisDetonationPdu::_bind_methods() {
	ClassDB::bind_static_method("DisDetonationPdu", D_METHOD("decode", "bytes"), &DisDetonationPdu::decode);
	ADD_GROUP("Detonation", "");
	DIS_BIND_PROPERTY(DisDetonationPdu, VECTOR3, location_in_entity);
	DIS_BIND_PROPERTY(DisDetonationPdu, INT, detonation_result);
	DIS_BIND_PROPERTY(DisDetonationPdu, INT, padding);
	DIS_BIND_PROPERTY(DisDetonationPdu, ARRAY, variable_parameters);
	ADD_GROUP("", "");

	BIND_ENUM_CONSTANT(RESULT_OTHER);
	BIND_ENUM_CONSTANT(RESULT_ENTITY_IMPACT);
	BIND_ENUM_CONSTANT(RESULT_ENTITY_PROXIMATE_DETONATION);
	BIND_ENUM_CONSTANT(RESULT_GROUND_IMPACT);
	BIND_ENUM_CONSTANT(RESULT_GROUND_PROXIMATE_DETONATION);
	BIND_ENUM_CONSTANT(RESULT_DETONATION);
	BIND_ENUM_CONSTANT(RESULT_NONE_OR_DUD);
	BIND_ENUM_CONSTANT(RESULT_AIR_HIT);
	BIND_ENUM_CONSTANT(RESULT_WATER_IMPACT);
	BIND_ENUM_CONSTANT(RESULT_AIR_BURST);
}

int DisDetonationPdu::get_length() const {
	return BASE_SIZE + VARIABLE_PARAMETER_SIZE * static_cast<int>(variable_parameters.size());
}

PackedByteArray DisDetonationPdu::encode() const {
	DIS::DetonationPdu pdu;
	write_header(pdu);
	pdu.setFiringEntityID(to_dis_entity_id(firing_entity_id));
	pdu.setTargetEntityID(to_dis_entity_id(target_entity_id));
	pdu.setExplodingEntityID(to_dis_entity_id(munition_id));
	pdu.setEventID(to_dis_event_id(event_id));
	pdu.setVelocity(to_dis_vector(velocity));
	pdu.setLocationInWorldCoordinates(to_dis_location(location_ecef));
	pdu.setDescriptor(to_dis_descriptor(munition_type, warhead, fuse, quantity, rate));
	pdu.setLocationOfEntityCoordinates(to_dis_vector(location_in_entity));
	pdu.setDetonationResult(static_cast<unsigned char>(detonation_result));
	pdu.setPad(static_cast<unsigned short>(padding));

	std::vector<DIS::VariableParameter> parameters;
	for (int64_t i = 0; i < variable_parameters.size(); i++) {
		const PackedByteArray raw = variable_parameters[i];
		ERR_CONTINUE_MSG(raw.size() != VARIABLE_PARAMETER_SIZE, "variable parameters must be 16 bytes each");
		DIS::VariableParameter parameter;
		DIS::DataStream stream = pdu_bytes::to_stream(raw);
		parameter.unmarshal(stream);
		parameters.push_back(parameter);
	}
	pdu.setVariableParameters(parameters);
	return marshal(pdu);
}

Ref<DisDetonationPdu> DisDetonationPdu::decode(const PackedByteArray &bytes) {
	if (!DisPdu::validate(bytes, DisPdu::PDU_TYPE_DETONATION, BASE_SIZE)) {
		return Ref<DisDetonationPdu>();
	}
	// Byte 101 is the variable parameter count; make sure they are all inside the packet.
	const int count = bytes[101];
	const int needed = BASE_SIZE + VARIABLE_PARAMETER_SIZE * count;
	if (bytes.size() < needed) {
		DisPdu::set_last_error(vformat("%d variable parameters need %d bytes, got %d", count, needed, bytes.size()));
		return Ref<DisDetonationPdu>();
	}
	DIS::DetonationPdu pdu;
	DIS::DataStream stream = pdu_bytes::to_stream(bytes.slice(0, needed));
	pdu.unmarshal(stream);

	Ref<DisDetonationPdu> out;
	out.instantiate();
	out->read_header(pdu);
	out->firing_entity_id = from_dis_entity_id(pdu.getFiringEntityID());
	out->target_entity_id = from_dis_entity_id(pdu.getTargetEntityID());
	out->munition_id = from_dis_entity_id(pdu.getExplodingEntityID());
	out->event_id = from_dis_event_id(pdu.getEventID());
	out->velocity = from_dis_vector(pdu.getVelocity());
	out->location_ecef = from_dis_location(pdu.getLocationInWorldCoordinates());
	DIS_READ_DESCRIPTOR(out, pdu.getDescriptor());
	out->location_in_entity = from_dis_vector(pdu.getLocationOfEntityCoordinates());
	out->detonation_result = pdu.getDetonationResult();
	out->padding = pdu.getPad();
	for (int i = 0; i < count; i++) {
		const int64_t start = BASE_SIZE + VARIABLE_PARAMETER_SIZE * i;
		out->variable_parameters.push_back(bytes.slice(start, start + VARIABLE_PARAMETER_SIZE));
	}
	return out;
}
