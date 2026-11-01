#include "dis_datum_pdus.h"

#include <initializer_list>

#include <godot_cpp/core/class_db.hpp>

#include "dis_pdu.h"

using namespace godot;

namespace {
constexpr int HEADER_AND_IDS = 24;

int64_t padded8(int64_t bytes) {
	return (bytes + 7) / 8 * 8;
}

int64_t bytes_for_bits(int64_t bits) {
	return (bits + 7) / 8;
}
} // namespace

void DisDatumPdu::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add_fixed_datum", "id", "value"), &DisDatumPdu::add_fixed_datum);
	ClassDB::bind_method(D_METHOD("add_variable_datum", "id", "data", "length_bits"), &DisDatumPdu::add_variable_datum,
			DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("add_text", "id", "text"), &DisDatumPdu::add_text);
	ClassDB::bind_method(D_METHOD("get_text", "id"), &DisDatumPdu::get_text);
	ADD_GROUP("Datums", "");
	DIS_BIND_PROPERTY(DisDatumPdu, ARRAY, fixed_datums);
	DIS_BIND_PROPERTY(DisDatumPdu, ARRAY, variable_datums);
	DIS_BIND_PROPERTY(DisDatumPdu, INT, padding);
	ADD_GROUP("", "");
}

void DisDatumPdu::add_fixed_datum(int64_t id, int64_t value) {
	Dictionary datum;
	datum["id"] = id;
	datum["value"] = value;
	fixed_datums.push_back(datum);
}

void DisDatumPdu::add_variable_datum(int64_t id, const PackedByteArray &data, int64_t length_bits) {
	Dictionary datum;
	datum["id"] = id;
	datum["length"] = length_bits >= 0 ? length_bits : data.size() * 8;
	datum["data"] = data;
	variable_datums.push_back(datum);
}

void DisDatumPdu::add_text(int64_t id, const String &text) {
	add_variable_datum(id, text.to_utf8_buffer(), -1);
}

String DisDatumPdu::get_text(int64_t id) const {
	for (int64_t i = 0; i < variable_datums.size(); i++) {
		const Dictionary datum = variable_datums[i];
		if (static_cast<int64_t>(datum.get("id", -1)) == id) {
			const PackedByteArray data = datum.get("data", PackedByteArray());
			return data.get_string_from_utf8();
		}
	}
	return String();
}

int DisDatumPdu::get_body_length() const {
	int64_t length = HEADER_AND_IDS + (has_request() ? 8 : 0) + 8 + 8 * fixed_datums.size();
	for (int64_t i = 0; i < variable_datums.size(); i++) {
		const Dictionary datum = variable_datums[i];
		length += 8 + padded8(bytes_for_bits(datum.get("length", 0)));
	}
	return static_cast<int>(length);
}

PackedByteArray DisDatumPdu::encode() const {
	byte_io::Writer out;
	write_header(out);
	for (const Vector3i &id : { originating_id, receiving_id }) {
		out.u16(id.x);
		out.u16(id.y);
		out.u16(id.z);
	}
	if (has_request()) {
		out.u32(static_cast<uint64_t>(request_id));
		out.u32(static_cast<uint64_t>(padding));
	}
	out.u32(fixed_datums.size());
	out.u32(variable_datums.size());
	for (int64_t i = 0; i < fixed_datums.size(); i++) {
		const Dictionary datum = fixed_datums[i];
		out.u32(static_cast<int64_t>(datum.get("id", 0)));
		out.u32(static_cast<int64_t>(datum.get("value", 0)));
	}
	for (int64_t i = 0; i < variable_datums.size(); i++) {
		const Dictionary datum = variable_datums[i];
		const int64_t bits = datum.get("length", 0);
		PackedByteArray data = datum.get("data", PackedByteArray());
		const int64_t bytes = bytes_for_bits(bits);
		ERR_CONTINUE_MSG(data.size() < bytes, "variable datum data is shorter than its length");
		out.u32(static_cast<int64_t>(datum.get("id", 0)));
		out.u32(bits);
		out.raw(data.slice(0, bytes));
		out.zeros(padded8(bytes) - bytes);
	}
	return finish(out);
}

bool DisDatumPdu::decode_into(DisDatumPdu *out, const PackedByteArray &bytes, int type) {
	const int minimum = HEADER_AND_IDS + (out->has_request() ? 8 : 0) + 8;
	if (!DisPdu::validate(bytes, type, minimum)) {
		return false;
	}
	const int length = DisPdu::get_effective_length(bytes);
	byte_io::Reader in(bytes, 0, length);
	out->read_header(in);
	Vector3i ids[2];
	for (Vector3i &id : ids) {
		id.x = static_cast<int32_t>(in.u16());
		id.y = static_cast<int32_t>(in.u16());
		id.z = static_cast<int32_t>(in.u16());
	}
	out->originating_id = ids[0];
	out->receiving_id = ids[1];
	if (out->has_request()) {
		out->request_id = static_cast<int64_t>(in.u32());
		out->padding = static_cast<int64_t>(in.u32());
	}
	const uint64_t fixed_count = in.u32();
	const uint64_t variable_count = in.u32();
	if (fixed_count * 8 + variable_count * 8 > static_cast<uint64_t>(in.remaining())) {
		DisPdu::set_last_error(vformat("%d fixed and %d variable datums do not fit in %d bytes", fixed_count,
				variable_count, in.remaining()));
		return false;
	}
	for (uint64_t i = 0; i < fixed_count; i++) {
		const int64_t id = static_cast<int64_t>(in.u32());
		out->add_fixed_datum(id, static_cast<int64_t>(in.u32()));
	}
	for (uint64_t i = 0; i < variable_count && in.ok(); i++) {
		const int64_t id = static_cast<int64_t>(in.u32());
		const int64_t bits = static_cast<int64_t>(in.u32());
		const int64_t data_bytes = bytes_for_bits(bits);
		const PackedByteArray data = in.raw(data_bytes);
		in.skip(padded8(data_bytes) - data_bytes);
		out->add_variable_datum(id, data, bits);
	}
	if (!in.ok()) {
		DisPdu::set_last_error("variable datum runs past the end of the PDU");
		return false;
	}
	out->keep_trailing(bytes);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------

void DisCommentPdu::_bind_methods() {
	ClassDB::bind_static_method("DisCommentPdu", D_METHOD("decode", "bytes"), &DisCommentPdu::decode);
}

Ref<DisCommentPdu> DisCommentPdu::decode(const PackedByteArray &bytes) {
	Ref<DisCommentPdu> out;
	out.instantiate();
	return decode_into(out.ptr(), bytes, DisPdu::PDU_TYPE_COMMENT) ? out : Ref<DisCommentPdu>();
}

void DisSetDataPdu::_bind_methods() {
	ClassDB::bind_static_method("DisSetDataPdu", D_METHOD("decode", "bytes"), &DisSetDataPdu::decode);
}

Ref<DisSetDataPdu> DisSetDataPdu::decode(const PackedByteArray &bytes) {
	Ref<DisSetDataPdu> out;
	out.instantiate();
	return decode_into(out.ptr(), bytes, DisPdu::PDU_TYPE_SET_DATA) ? out : Ref<DisSetDataPdu>();
}

void DisDataPdu::_bind_methods() {
	ClassDB::bind_static_method("DisDataPdu", D_METHOD("decode", "bytes"), &DisDataPdu::decode);
}

Ref<DisDataPdu> DisDataPdu::decode(const PackedByteArray &bytes) {
	Ref<DisDataPdu> out;
	out.instantiate();
	return decode_into(out.ptr(), bytes, DisPdu::PDU_TYPE_DATA) ? out : Ref<DisDataPdu>();
}
