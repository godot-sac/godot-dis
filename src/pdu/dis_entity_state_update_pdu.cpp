#include "dis_entity_state_update_pdu.h"

#include <godot_cpp/core/class_db.hpp>

#include "byte_io.h"
#include "dis_pdu.h"
#include "pdu_convert.h"

using namespace godot;

DisEntityStateUpdatePdu::DisEntityStateUpdatePdu() {
	location_ecef = pdu_convert::zero_location();
}

void DisEntityStateUpdatePdu::_bind_methods() {
	ClassDB::bind_static_method("DisEntityStateUpdatePdu", D_METHOD("decode", "bytes"), &DisEntityStateUpdatePdu::decode);
	ADD_GROUP("Entity State Update", "");
	DIS_BIND_PROPERTY(DisEntityStateUpdatePdu, VECTOR3I, entity_id);
	DIS_BIND_PROPERTY(DisEntityStateUpdatePdu, VECTOR3, linear_velocity);
	DIS_BIND_PROPERTY(DisEntityStateUpdatePdu, PACKED_FLOAT64_ARRAY, location_ecef);
	DIS_BIND_PROPERTY(DisEntityStateUpdatePdu, VECTOR3, orientation);
	DIS_BIND_PROPERTY(DisEntityStateUpdatePdu, INT, appearance);
	DIS_BIND_PROPERTY(DisEntityStateUpdatePdu, ARRAY, variable_parameters);
	ADD_GROUP("", "");
}

void DisEntityStateUpdatePdu::set_location_ecef(const PackedFloat64Array &value) {
	ERR_FAIL_COND_MSG(value.size() != 3, "location_ecef needs 3 values (x, y, z)");
	location_ecef = value;
}

int DisEntityStateUpdatePdu::get_body_length() const {
	return BASE_SIZE + VARIABLE_PARAMETER_SIZE * static_cast<int>(variable_parameters.size());
}

PackedByteArray DisEntityStateUpdatePdu::encode() const {
	ERR_FAIL_COND_V_MSG(variable_parameters.size() > 255, PackedByteArray(),
			vformat("%d variable parameters; the count field allows at most 255", variable_parameters.size()));
	for (int64_t i = 0; i < variable_parameters.size(); i++) {
		ERR_FAIL_COND_V_MSG(PackedByteArray(variable_parameters[i]).size() != VARIABLE_PARAMETER_SIZE, PackedByteArray(),
				vformat("variable parameter %d is not 16 bytes", i));
	}
	byte_io::Writer out;
	write_header(out);
	out.u16(entity_id.x);
	out.u16(entity_id.y);
	out.u16(entity_id.z);
	out.u8(0); // padding
	out.u8(variable_parameters.size());
	out.f32(linear_velocity.x);
	out.f32(linear_velocity.y);
	out.f32(linear_velocity.z);
	for (int i = 0; i < 3; i++) {
		out.f64(location_ecef[i]);
	}
	out.f32(orientation.x);
	out.f32(orientation.y);
	out.f32(orientation.z);
	out.u32(static_cast<uint64_t>(appearance) & 0xFFFFFFFF);
	for (int64_t i = 0; i < variable_parameters.size(); i++) {
		out.raw(variable_parameters[i]);
	}
	return finish(out);
}

Ref<DisEntityStateUpdatePdu> DisEntityStateUpdatePdu::decode(const PackedByteArray &bytes) {
	if (!DisPdu::validate(bytes, DisPdu::PDU_TYPE_ENTITY_STATE_UPDATE, BASE_SIZE)) {
		return Ref<DisEntityStateUpdatePdu>();
	}
	const int64_t length = DisPdu::get_effective_length(bytes);
	byte_io::Reader in(bytes, 0, length);
	Ref<DisEntityStateUpdatePdu> out;
	out.instantiate();
	out->read_header(in);
	const int site = static_cast<int>(in.u16());
	const int application = static_cast<int>(in.u16());
	out->entity_id = Vector3i(site, application, static_cast<int>(in.u16()));
	in.skip(1);
	const int count = static_cast<int>(in.u8());
	const double vx = in.f32();
	const double vy = in.f32();
	out->linear_velocity = Vector3(vx, vy, in.f32());
	for (int i = 0; i < 3; i++) {
		out->location_ecef.set(i, in.f64());
	}
	const double psi = in.f32();
	const double theta = in.f32();
	out->orientation = Vector3(psi, theta, in.f32());
	out->appearance = static_cast<int64_t>(in.u32());
	if (BASE_SIZE + VARIABLE_PARAMETER_SIZE * count > length) {
		DisPdu::set_last_error(vformat("%d variable parameters do not fit in a %d byte PDU", count, length));
		return Ref<DisEntityStateUpdatePdu>();
	}
	for (int i = 0; i < count; i++) {
		out->variable_parameters.push_back(in.raw(VARIABLE_PARAMETER_SIZE));
	}
	if (!in.ok()) {
		DisPdu::set_last_error("Entity State Update PDU runs past its length");
		return Ref<DisEntityStateUpdatePdu>();
	}
	out->keep_trailing(bytes);
	return out;
}
