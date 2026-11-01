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
	DIS_PROPERTY(PACKED_BYTE_ARRAY, trailing_bytes);
#undef DIS_PROPERTY
}

void DisPduBase::write_header(DIS::Pdu &pdu) const {
	pdu.setProtocolVersion(static_cast<unsigned char>(protocol_version));
	pdu.setExerciseID(static_cast<unsigned char>(exercise_id));
	pdu.setTimestamp(static_cast<unsigned int>(timestamp));
	pdu.setPduStatus(static_cast<unsigned char>(pdu_status));
	pdu.setProtocolFamily(static_cast<unsigned char>(header_family()));
}

void DisPduBase::read_header(const DIS::Pdu &pdu) {
	protocol_version = pdu.getProtocolVersion();
	exercise_id = pdu.getExerciseID();
	timestamp = pdu.getTimestamp();
	pdu_status = pdu.getPduStatus();
	received_family = pdu.getProtocolFamily() != get_protocol_family() ? pdu.getProtocolFamily() : -1;
}

PackedByteArray DisPduBase::marshal(DIS::Pdu &pdu) const {
	pdu.setLength(static_cast<unsigned short>(pdu.getMarshalledSize() + trailing_bytes.size()));
	DIS::DataStream stream(DIS::BIG);
	pdu.marshal(stream);
	PackedByteArray bytes = pdu_bytes::from_stream(stream);
	bytes.append_array(trailing_bytes);
	return bytes;
}

PackedByteArray DisPduBase::finish(byte_io::Writer &out) const {
	out.raw(trailing_bytes);
	return out.finish();
}

void DisPduBase::keep_trailing(const PackedByteArray &bytes) {
	const int64_t body = get_body_length();
	int64_t length = bytes.size() >= 10 ? ((bytes[8] << 8) | bytes[9]) : 0;
	if (length == 0) {
		length = bytes.size();
	}
	trailing_bytes = length > body ? bytes.slice(body, length) : PackedByteArray();
}

void DisPduBase::write_header(byte_io::Writer &out) const {
	out.u8(protocol_version);
	out.u8(exercise_id);
	out.u8(get_pdu_type());
	out.u8(header_family());
	out.u32(static_cast<uint64_t>(timestamp));
	out.u16(0); // length, set by finish()
	out.u8(pdu_status);
	out.u8(0); // padding
}

void DisPduBase::read_header(byte_io::Reader &in) {
	protocol_version = static_cast<int>(in.u8());
	exercise_id = static_cast<int>(in.u8());
	in.skip(1); // type, checked by DisPdu::validate()
	const int family = static_cast<int>(in.u8());
	received_family = family != get_protocol_family() ? family : -1;
	timestamp = static_cast<int64_t>(in.u32());
	in.skip(2); // length
	pdu_status = static_cast<int>(in.u8());
	in.skip(1);
}
