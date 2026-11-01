#include "dis_simulation_management_pdus.h"

#include <cmath>

#include <dis7/AcknowledgePdu.h>
#include <dis7/CreateEntityPdu.h>
#include <dis7/RemoveEntityPdu.h>
#include <dis7/StartResumePdu.h>
#include <dis7/StopFreezePdu.h>

#include <godot_cpp/core/class_db.hpp>

#include "dis_pdu.h"
#include "pdu_bytes.h"
#include "pdu_convert.h"

using namespace godot;
using namespace pdu_convert;

// ---------------------------------------------------------------------------------------------------------------
// Shared fields

void DisSimulationManagementPdu::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_addressed_to", "site", "application"), &DisSimulationManagementPdu::is_addressed_to);
	ADD_GROUP("Simulation Management", "");
	DIS_BIND_PROPERTY(DisSimulationManagementPdu, VECTOR3I, originating_id);
	DIS_BIND_PROPERTY(DisSimulationManagementPdu, VECTOR3I, receiving_id);
	DIS_BIND_PROPERTY(DisSimulationManagementPdu, INT, request_id);
	ADD_GROUP("", "");
	BIND_CONSTANT(ID_ALL);
}

bool DisSimulationManagementPdu::is_addressed_to(int site, int application) const {
	const bool site_matches = receiving_id.x == ID_ALL || receiving_id.x == site;
	const bool application_matches = receiving_id.y == ID_ALL || receiving_id.y == application;
	return site_matches && application_matches;
}

namespace {
// Copies the IDs between the Godot object and the open-dis-cpp PDU (both start with them).
template <typename Pdu>
void write_ids(Pdu &pdu, const Vector3i &originating, const Vector3i &receiving) {
	pdu.setOriginatingEntityID(to_dis_entity_id(originating));
	pdu.setReceivingEntityID(to_dis_entity_id(receiving));
}

template <typename Pdu>
Ref<Pdu> checked(const PackedByteArray &bytes, int type, int size) {
	if (!DisPdu::validate(bytes, type, size)) {
		return Ref<Pdu>();
	}
	Ref<Pdu> out;
	out.instantiate();
	return out;
}
} // namespace

#define DIS_READ_IDS(out, pdu)                                                  \
	(out)->originating_id = from_dis_entity_id((pdu).getOriginatingEntityID()); \
	(out)->receiving_id = from_dis_entity_id((pdu).getReceivingEntityID())

// ---------------------------------------------------------------------------------------------------------------
// Clock time

namespace dis_clock {
double to_seconds(int64_t hour, int64_t time_past_hour) {
	// The hour field is a signed 32-bit integer.
	const int32_t signed_hour = static_cast<int32_t>(static_cast<uint32_t>(hour));
	return static_cast<double>(signed_hour) * 3600.0 + DisPdu::timestamp_to_seconds(time_past_hour);
}

void from_seconds(double seconds, bool absolute, int64_t &hour, int64_t &time_past_hour) {
	const double hours = std::floor(seconds / 3600.0);
	hour = static_cast<int64_t>(hours);
	time_past_hour = DisPdu::timestamp_from_seconds(seconds - hours * 3600.0, absolute);
}
} // namespace dis_clock

// ---------------------------------------------------------------------------------------------------------------
// Start/Resume

void DisStartResumePdu::_bind_methods() {
	ClassDB::bind_static_method("DisStartResumePdu", D_METHOD("decode", "bytes"), &DisStartResumePdu::decode);
	ADD_GROUP("Start/Resume", "");
	DIS_BIND_PROPERTY(DisStartResumePdu, INT, real_world_time_hour);
	DIS_BIND_PROPERTY(DisStartResumePdu, INT, real_world_time_past_hour);
	DIS_BIND_PROPERTY(DisStartResumePdu, INT, simulation_time_hour);
	DIS_BIND_PROPERTY(DisStartResumePdu, INT, simulation_time_past_hour);
	// Convenience views of the fields above; not stored separately.
	ClassDB::bind_method(D_METHOD("set_real_world_time", "unix_seconds"), &DisStartResumePdu::set_real_world_time);
	ClassDB::bind_method(D_METHOD("get_real_world_time"), &DisStartResumePdu::get_real_world_time);
	ClassDB::bind_method(D_METHOD("set_simulation_time", "seconds"), &DisStartResumePdu::set_simulation_time);
	ClassDB::bind_method(D_METHOD("get_simulation_time"), &DisStartResumePdu::get_simulation_time);
	ADD_GROUP("", "");
}

void DisStartResumePdu::set_real_world_time(double seconds) {
	dis_clock::from_seconds(seconds, true, real_world_time_hour, real_world_time_past_hour);
}

double DisStartResumePdu::get_real_world_time() const {
	return dis_clock::to_seconds(real_world_time_hour, real_world_time_past_hour);
}

void DisStartResumePdu::set_simulation_time(double seconds) {
	dis_clock::from_seconds(seconds, false, simulation_time_hour, simulation_time_past_hour);
}

double DisStartResumePdu::get_simulation_time() const {
	return dis_clock::to_seconds(simulation_time_hour, simulation_time_past_hour);
}

PackedByteArray DisStartResumePdu::encode() const {
	DIS::StartResumePdu pdu;
	write_header(pdu);
	write_ids(pdu, originating_id, receiving_id);
	pdu.setRealWorldTime(to_dis_clock(real_world_time_hour, real_world_time_past_hour));
	pdu.setSimulationTime(to_dis_clock(simulation_time_hour, simulation_time_past_hour));
	pdu.setRequestID(static_cast<unsigned int>(request_id));
	return marshal(pdu);
}

Ref<DisStartResumePdu> DisStartResumePdu::decode(const PackedByteArray &bytes) {
	Ref<DisStartResumePdu> out = checked<DisStartResumePdu>(bytes, DisPdu::PDU_TYPE_START_RESUME, SIZE);
	if (out.is_null()) {
		return out;
	}
	DIS::StartResumePdu pdu;
	DIS::DataStream stream = pdu_bytes::to_stream(bytes.slice(0, SIZE));
	pdu.unmarshal(stream);
	out->read_header(pdu);
	DIS_READ_IDS(out, pdu);
	out->real_world_time_hour = static_cast<int32_t>(pdu.getRealWorldTime().getHour());
	out->real_world_time_past_hour = pdu.getRealWorldTime().getTimePastHour();
	out->simulation_time_hour = static_cast<int32_t>(pdu.getSimulationTime().getHour());
	out->simulation_time_past_hour = pdu.getSimulationTime().getTimePastHour();
	out->request_id = pdu.getRequestID();
	out->keep_trailing(bytes);
	return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Stop/Freeze

void DisStopFreezePdu::_bind_methods() {
	ClassDB::bind_static_method("DisStopFreezePdu", D_METHOD("decode", "bytes"), &DisStopFreezePdu::decode);
	ADD_GROUP("Stop/Freeze", "");
	DIS_BIND_PROPERTY(DisStopFreezePdu, INT, real_world_time_hour);
	DIS_BIND_PROPERTY(DisStopFreezePdu, INT, real_world_time_past_hour);
	DIS_BIND_PROPERTY(DisStopFreezePdu, INT, reason);
	DIS_BIND_PROPERTY(DisStopFreezePdu, INT, frozen_behavior);
	DIS_BIND_PROPERTY(DisStopFreezePdu, INT, padding);
	ClassDB::bind_method(D_METHOD("set_real_world_time", "unix_seconds"), &DisStopFreezePdu::set_real_world_time);
	ClassDB::bind_method(D_METHOD("get_real_world_time"), &DisStopFreezePdu::get_real_world_time);
	ADD_GROUP("", "");

	BIND_ENUM_CONSTANT(REASON_OTHER);
	BIND_ENUM_CONSTANT(REASON_RECESS);
	BIND_ENUM_CONSTANT(REASON_TERMINATION);
	BIND_ENUM_CONSTANT(REASON_SYSTEM_FAILURE);
	BIND_ENUM_CONSTANT(REASON_SECURITY_VIOLATION);
	BIND_ENUM_CONSTANT(REASON_ENTITY_RECONSTITUTION);
	BIND_ENUM_CONSTANT(REASON_STOP_FOR_RESET);
	BIND_ENUM_CONSTANT(REASON_STOP_FOR_RESTART);
	BIND_ENUM_CONSTANT(REASON_ABORT_TRAINING_RETURN_TO_TACTICAL_OPERATIONS);
	BIND_ENUM_CONSTANT(FROZEN_RUN_SIMULATION_CLOCK);
	BIND_ENUM_CONSTANT(FROZEN_TRANSMIT_UPDATES);
	BIND_ENUM_CONSTANT(FROZEN_PROCESS_UPDATES);
}

void DisStopFreezePdu::set_real_world_time(double seconds) {
	dis_clock::from_seconds(seconds, true, real_world_time_hour, real_world_time_past_hour);
}

double DisStopFreezePdu::get_real_world_time() const {
	return dis_clock::to_seconds(real_world_time_hour, real_world_time_past_hour);
}

PackedByteArray DisStopFreezePdu::encode() const {
	DIS::StopFreezePdu pdu;
	write_header(pdu);
	write_ids(pdu, originating_id, receiving_id);
	pdu.setRealWorldTime(to_dis_clock(real_world_time_hour, real_world_time_past_hour));
	pdu.setReason(static_cast<unsigned char>(reason));
	pdu.setFrozenBehavior(static_cast<unsigned char>(frozen_behavior));
	pdu.setPadding1(static_cast<short>(padding));
	pdu.setRequestID(static_cast<unsigned int>(request_id));
	return marshal(pdu);
}

Ref<DisStopFreezePdu> DisStopFreezePdu::decode(const PackedByteArray &bytes) {
	Ref<DisStopFreezePdu> out = checked<DisStopFreezePdu>(bytes, DisPdu::PDU_TYPE_STOP_FREEZE, SIZE);
	if (out.is_null()) {
		return out;
	}
	DIS::StopFreezePdu pdu;
	DIS::DataStream stream = pdu_bytes::to_stream(bytes.slice(0, SIZE));
	pdu.unmarshal(stream);
	out->read_header(pdu);
	DIS_READ_IDS(out, pdu);
	out->real_world_time_hour = static_cast<int32_t>(pdu.getRealWorldTime().getHour());
	out->real_world_time_past_hour = pdu.getRealWorldTime().getTimePastHour();
	out->reason = pdu.getReason();
	out->frozen_behavior = pdu.getFrozenBehavior();
	out->padding = static_cast<uint16_t>(pdu.getPadding1());
	out->request_id = pdu.getRequestID();
	out->keep_trailing(bytes);
	return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Acknowledge

void DisAcknowledgePdu::_bind_methods() {
	ClassDB::bind_static_method("DisAcknowledgePdu", D_METHOD("decode", "bytes"), &DisAcknowledgePdu::decode);
	ADD_GROUP("Acknowledge", "");
	DIS_BIND_PROPERTY(DisAcknowledgePdu, INT, acknowledge_flag);
	DIS_BIND_PROPERTY(DisAcknowledgePdu, INT, response_flag);
	ADD_GROUP("", "");

	BIND_ENUM_CONSTANT(ACKNOWLEDGE_CREATE_ENTITY);
	BIND_ENUM_CONSTANT(ACKNOWLEDGE_REMOVE_ENTITY);
	BIND_ENUM_CONSTANT(ACKNOWLEDGE_START_RESUME);
	BIND_ENUM_CONSTANT(ACKNOWLEDGE_STOP_FREEZE);
	BIND_ENUM_CONSTANT(ACKNOWLEDGE_TRANSFER_OWNERSHIP);
	BIND_ENUM_CONSTANT(RESPONSE_OTHER);
	BIND_ENUM_CONSTANT(RESPONSE_ABLE_TO_COMPLY);
	BIND_ENUM_CONSTANT(RESPONSE_UNABLE_TO_COMPLY);
	BIND_ENUM_CONSTANT(RESPONSE_PENDING_OPERATOR_ACTION);
	BIND_ENUM_CONSTANT(RESPONSE_REQUEST_FURTHER_INFORMATION);
}

PackedByteArray DisAcknowledgePdu::encode() const {
	DIS::AcknowledgePdu pdu;
	write_header(pdu);
	write_ids(pdu, originating_id, receiving_id);
	pdu.setAcknowledgeFlag(static_cast<unsigned short>(acknowledge_flag));
	pdu.setResponseFlag(static_cast<unsigned short>(response_flag));
	pdu.setRequestID(static_cast<unsigned int>(request_id));
	return marshal(pdu);
}

Ref<DisAcknowledgePdu> DisAcknowledgePdu::decode(const PackedByteArray &bytes) {
	Ref<DisAcknowledgePdu> out = checked<DisAcknowledgePdu>(bytes, DisPdu::PDU_TYPE_ACKNOWLEDGE, SIZE);
	if (out.is_null()) {
		return out;
	}
	DIS::AcknowledgePdu pdu;
	DIS::DataStream stream = pdu_bytes::to_stream(bytes.slice(0, SIZE));
	pdu.unmarshal(stream);
	out->read_header(pdu);
	DIS_READ_IDS(out, pdu);
	out->acknowledge_flag = pdu.getAcknowledgeFlag();
	out->response_flag = pdu.getResponseFlag();
	out->request_id = pdu.getRequestID();
	out->keep_trailing(bytes);
	return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Remove Entity

void DisRemoveEntityPdu::_bind_methods() {
	ClassDB::bind_static_method("DisRemoveEntityPdu", D_METHOD("decode", "bytes"), &DisRemoveEntityPdu::decode);
}

PackedByteArray DisRemoveEntityPdu::encode() const {
	DIS::RemoveEntityPdu pdu;
	write_header(pdu);
	write_ids(pdu, originating_id, receiving_id);
	pdu.setRequestID(static_cast<unsigned int>(request_id));
	return marshal(pdu);
}

Ref<DisRemoveEntityPdu> DisRemoveEntityPdu::decode(const PackedByteArray &bytes) {
	Ref<DisRemoveEntityPdu> out = checked<DisRemoveEntityPdu>(bytes, DisPdu::PDU_TYPE_REMOVE_ENTITY, SIZE);
	if (out.is_null()) {
		return out;
	}
	DIS::RemoveEntityPdu pdu;
	DIS::DataStream stream = pdu_bytes::to_stream(bytes.slice(0, SIZE));
	pdu.unmarshal(stream);
	out->read_header(pdu);
	DIS_READ_IDS(out, pdu);
	out->request_id = pdu.getRequestID();
	out->keep_trailing(bytes);
	return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Create Entity

void DisCreateEntityPdu::_bind_methods() {
	ClassDB::bind_static_method("DisCreateEntityPdu", D_METHOD("decode", "bytes"), &DisCreateEntityPdu::decode);
}

PackedByteArray DisCreateEntityPdu::encode() const {
	DIS::CreateEntityPdu pdu;
	write_header(pdu);
	write_ids(pdu, originating_id, receiving_id);
	pdu.setRequestID(static_cast<unsigned int>(request_id));
	return marshal(pdu);
}

Ref<DisCreateEntityPdu> DisCreateEntityPdu::decode(const PackedByteArray &bytes) {
	Ref<DisCreateEntityPdu> out = checked<DisCreateEntityPdu>(bytes, DisPdu::PDU_TYPE_CREATE_ENTITY, SIZE);
	if (out.is_null()) {
		return out;
	}
	DIS::CreateEntityPdu pdu;
	DIS::DataStream stream = pdu_bytes::to_stream(bytes.slice(0, SIZE));
	pdu.unmarshal(stream);
	out->read_header(pdu);
	DIS_READ_IDS(out, pdu);
	out->request_id = pdu.getRequestID();
	out->keep_trailing(bytes);
	return out;
}
