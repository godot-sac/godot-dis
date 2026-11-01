#include "dis_pdu_base.h"

#include <dis7/Pdu.h>
#include <dis7/utils/DataStream.h>

#include <godot_cpp/core/class_db.hpp>

#include "pdu_bytes.h"

using namespace godot;

void DisPduBase::_bind_methods() {
	ClassDB::bind_method(D_METHOD("encode"), &DisPduBase::encode);
	ClassDB::bind_method(D_METHOD("get_pdu_type"), &DisPduBase::get_pdu_type);
	ClassDB::bind_method(D_METHOD("get_protocol_family"), &DisPduBase::get_protocol_family);
	ClassDB::bind_method(D_METHOD("get_length"), &DisPduBase::get_length);

#define DIS_PROPERTY(variant_type, name)                                                  \
	ClassDB::bind_method(D_METHOD("set_" #name, "value"), &DisPduBase::set_##name); \
	ClassDB::bind_method(D_METHOD("get_" #name), &DisPduBase::get_##name);          \
	ADD_PROPERTY(PropertyInfo(Variant::variant_type, #name), "set_" #name, "get_" #name)

	ADD_GROUP("Header", "");
	DIS_PROPERTY(INT, protocol_version);
	DIS_PROPERTY(INT, exercise_id);
	DIS_PROPERTY(INT, timestamp);
	DIS_PROPERTY(INT, pdu_status);
	ADD_GROUP("", "");
#undef DIS_PROPERTY
}

void DisPduBase::write_header(DIS::Pdu &pdu) const {
	pdu.setProtocolVersion(static_cast<unsigned char>(protocol_version));
	pdu.setExerciseID(static_cast<unsigned char>(exercise_id));
	pdu.setTimestamp(static_cast<unsigned int>(timestamp));
	pdu.setPduStatus(static_cast<unsigned char>(pdu_status));
}

void DisPduBase::read_header(const DIS::Pdu &pdu) {
	protocol_version = pdu.getProtocolVersion();
	exercise_id = pdu.getExerciseID();
	timestamp = pdu.getTimestamp();
	pdu_status = pdu.getPduStatus();
}

PackedByteArray DisPduBase::marshal(DIS::Pdu &pdu) {
	pdu.setLength(static_cast<unsigned short>(pdu.getMarshalledSize()));
	DIS::DataStream stream(DIS::BIG);
	pdu.marshal(stream);
	return pdu_bytes::from_stream(stream);
}
