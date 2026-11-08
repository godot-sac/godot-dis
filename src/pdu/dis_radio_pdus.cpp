#include "dis_radio_pdus.h"

#include <algorithm>

#include <godot_cpp/core/class_db.hpp>

#include "dis_pdu.h"
#include "pdu_convert.h"

using namespace godot;

namespace {
void write_id(byte_io::Writer &out, const Vector3i &id) {
	out.u16(id.x);
	out.u16(id.y);
	out.u16(id.z);
}

Vector3i read_id(byte_io::Reader &in) {
	const int site = static_cast<int>(in.u16());
	const int application = static_cast<int>(in.u16());
	return Vector3i(site, application, static_cast<int>(in.u16()));
}

PackedInt32Array zeros(int count) {
	PackedInt32Array out;
	out.resize(count);
	out.fill(0);
	return out;
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Transmitter

DisTransmitterPdu::DisTransmitterPdu() {
	radio_type = zeros(6);
	modulation_type = zeros(4);
	antenna_location_ecef = pdu_convert::zero_location();
}

void DisTransmitterPdu::_bind_methods() {
	ClassDB::bind_static_method("DisTransmitterPdu", D_METHOD("decode", "bytes"), &DisTransmitterPdu::decode);
	ADD_GROUP("Transmitter", "");
	DIS_BIND_PROPERTY(DisTransmitterPdu, VECTOR3I, radio_reference_id);
	DIS_BIND_PROPERTY(DisTransmitterPdu, INT, radio_number);
	DIS_BIND_PROPERTY(DisTransmitterPdu, PACKED_INT32_ARRAY, radio_type);
	DIS_BIND_PROPERTY(DisTransmitterPdu, INT, transmit_state);
	DIS_BIND_PROPERTY(DisTransmitterPdu, INT, input_source);
	DIS_BIND_PROPERTY(DisTransmitterPdu, PACKED_FLOAT64_ARRAY, antenna_location_ecef);
	DIS_BIND_PROPERTY(DisTransmitterPdu, VECTOR3, relative_antenna_location);
	DIS_BIND_PROPERTY(DisTransmitterPdu, INT, antenna_pattern_type);
	DIS_BIND_PROPERTY(DisTransmitterPdu, INT, frequency);
	DIS_BIND_PROPERTY(DisTransmitterPdu, FLOAT, bandwidth);
	DIS_BIND_PROPERTY(DisTransmitterPdu, FLOAT, power);
	DIS_BIND_PROPERTY(DisTransmitterPdu, PACKED_INT32_ARRAY, modulation_type);
	DIS_BIND_PROPERTY(DisTransmitterPdu, INT, crypto_system);
	DIS_BIND_PROPERTY(DisTransmitterPdu, INT, crypto_key_id);
	DIS_BIND_PROPERTY(DisTransmitterPdu, PACKED_BYTE_ARRAY, modulation_parameters);
	DIS_BIND_PROPERTY(DisTransmitterPdu, PACKED_BYTE_ARRAY, antenna_pattern);
	DIS_BIND_PROPERTY(DisTransmitterPdu, ARRAY, variable_parameters);
	ADD_GROUP("", "");

	BIND_ENUM_CONSTANT(TRANSMIT_OFF);
	BIND_ENUM_CONSTANT(TRANSMIT_ON_NOT_TRANSMITTING);
	BIND_ENUM_CONSTANT(TRANSMIT_ON_AND_TRANSMITTING);
}

void DisTransmitterPdu::set_radio_type(const PackedInt32Array &value) {
	ERR_FAIL_COND_MSG(value.size() != 6, "radio_type needs 6 values (kind, domain, country, category, nomenclature version, nomenclature)");
	radio_type = value;
}

void DisTransmitterPdu::set_modulation_type(const PackedInt32Array &value) {
	ERR_FAIL_COND_MSG(value.size() != 4, "modulation_type needs 4 values (spread spectrum, major, detail, radio system)");
	modulation_type = value;
}

void DisTransmitterPdu::set_antenna_location_ecef(const PackedFloat64Array &value) {
	ERR_FAIL_COND_MSG(value.size() != 3, "antenna_location_ecef needs 3 values (x, y, z)");
	antenna_location_ecef = value;
}

int DisTransmitterPdu::get_body_length() const {
	int64_t length = BASE_SIZE + modulation_parameters.size() + antenna_pattern.size();
	for (int64_t i = 0; i < variable_parameters.size(); i++) {
		length += PackedByteArray(variable_parameters[i]).size();
	}
	return static_cast<int>(length);
}

PackedByteArray DisTransmitterPdu::encode() const {
	ERR_FAIL_COND_V_MSG(modulation_parameters.size() > 255, PackedByteArray(),
			vformat("%d bytes of modulation parameters; the length field allows at most 255", modulation_parameters.size()));
	ERR_FAIL_COND_V_MSG(antenna_pattern.size() > 0xFFFF || variable_parameters.size() > 0xFFFF, PackedByteArray(),
			"antenna pattern or variable parameters too long for their 16-bit fields");
	byte_io::Writer out;
	write_header(out);
	write_id(out, radio_reference_id);
	out.u16(radio_number);
	out.u8(radio_type[0]);
	out.u8(radio_type[1]);
	out.u16(radio_type[2]);
	out.u8(radio_type[3]);
	out.u8(radio_type[4]);
	out.u16(radio_type[5]);
	out.u8(transmit_state);
	out.u8(input_source);
	out.u16(variable_parameters.size());
	for (int i = 0; i < 3; i++) {
		out.f64(antenna_location_ecef[i]);
	}
	out.f32(relative_antenna_location.x);
	out.f32(relative_antenna_location.y);
	out.f32(relative_antenna_location.z);
	out.u16(antenna_pattern_type);
	out.u16(antenna_pattern.size());
	out.u64(static_cast<uint64_t>(frequency));
	out.f32(bandwidth);
	out.f32(power);
	for (int i = 0; i < 4; i++) {
		out.u16(modulation_type[i]);
	}
	out.u16(crypto_system);
	out.u16(crypto_key_id);
	out.u8(modulation_parameters.size());
	out.zeros(3);
	out.raw(modulation_parameters);
	out.raw(antenna_pattern);
	for (int64_t i = 0; i < variable_parameters.size(); i++) {
		out.raw(variable_parameters[i]);
	}
	return finish(out);
}

Ref<DisTransmitterPdu> DisTransmitterPdu::decode(const PackedByteArray &bytes) {
	if (!DisPdu::validate(bytes, DisPdu::PDU_TYPE_TRANSMITTER, BASE_SIZE)) {
		return Ref<DisTransmitterPdu>();
	}
	byte_io::Reader in(bytes, 0, DisPdu::get_effective_length(bytes));
	Ref<DisTransmitterPdu> out;
	out.instantiate();
	out->read_header(in);
	out->radio_reference_id = read_id(in);
	out->radio_number = static_cast<int>(in.u16());
	out->radio_type.set(0, static_cast<int>(in.u8()));
	out->radio_type.set(1, static_cast<int>(in.u8()));
	out->radio_type.set(2, static_cast<int>(in.u16()));
	out->radio_type.set(3, static_cast<int>(in.u8()));
	out->radio_type.set(4, static_cast<int>(in.u8()));
	out->radio_type.set(5, static_cast<int>(in.u16()));
	out->transmit_state = static_cast<int>(in.u8());
	out->input_source = static_cast<int>(in.u8());
	const int parameter_count = static_cast<int>(in.u16());
	for (int i = 0; i < 3; i++) {
		out->antenna_location_ecef.set(i, in.f64());
	}
	const double rx = in.f32();
	const double ry = in.f32();
	out->relative_antenna_location = Vector3(rx, ry, in.f32());
	out->antenna_pattern_type = static_cast<int>(in.u16());
	const int pattern_length = static_cast<int>(in.u16());
	out->frequency = static_cast<int64_t>(in.u64());
	out->bandwidth = in.f32();
	out->power = in.f32();
	for (int i = 0; i < 4; i++) {
		out->modulation_type.set(i, static_cast<int>(in.u16()));
	}
	out->crypto_system = static_cast<int>(in.u16());
	out->crypto_key_id = static_cast<int>(in.u16());
	const int modulation_length = static_cast<int>(in.u8());
	in.skip(3);
	out->modulation_parameters = in.raw(modulation_length);
	out->antenna_pattern = in.raw(pattern_length);
	// Variable transmitter parameter records: type (4), record length in bytes (2, the whole record), data.
	for (int i = 0; i < parameter_count && in.ok(); i++) {
		if (in.remaining() < 6) {
			DisPdu::set_last_error("variable transmitter parameter record runs past the end of the PDU");
			return Ref<DisTransmitterPdu>();
		}
		const int64_t start = in.position();
		in.skip(4);
		const int64_t record_length = static_cast<int64_t>(in.u16());
		if (record_length < 6 || start + record_length > start + 6 + in.remaining()) {
			DisPdu::set_last_error(vformat("bad variable transmitter parameter record length %d", record_length));
			return Ref<DisTransmitterPdu>();
		}
		in.skip(record_length - 6);
		out->variable_parameters.push_back(bytes.slice(start, start + record_length));
	}
	if (!in.ok()) {
		DisPdu::set_last_error("Transmitter PDU records run past the end of the PDU");
		return Ref<DisTransmitterPdu>();
	}
	out->keep_trailing(bytes);
	return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Signal

void DisSignalPdu::_bind_methods() {
	ClassDB::bind_static_method("DisSignalPdu", D_METHOD("decode", "bytes"), &DisSignalPdu::decode);
	ClassDB::bind_method(D_METHOD("set_data_bytes", "data"), &DisSignalPdu::set_data_bytes);
	ADD_GROUP("Signal", "");
	DIS_BIND_PROPERTY(DisSignalPdu, VECTOR3I, radio_reference_id);
	DIS_BIND_PROPERTY(DisSignalPdu, INT, radio_number);
	DIS_BIND_PROPERTY(DisSignalPdu, INT, encoding_scheme);
	DIS_BIND_PROPERTY(DisSignalPdu, INT, tdl_type);
	DIS_BIND_PROPERTY(DisSignalPdu, INT, sample_rate);
	DIS_BIND_PROPERTY(DisSignalPdu, INT, data_length);
	DIS_BIND_PROPERTY(DisSignalPdu, INT, samples);
	DIS_BIND_PROPERTY(DisSignalPdu, PACKED_BYTE_ARRAY, data);
	ADD_GROUP("", "");
}

void DisSignalPdu::set_data_bytes(const PackedByteArray &value) {
	// data_length is a 16-bit count of bits: at most 8191 whole bytes.
	ERR_FAIL_COND_MSG(value.size() > 8191, vformat("%d bytes of signal data; a Signal PDU carries at most 8191", value.size()));
	data = value;
	data_length = static_cast<int>(value.size() * 8);
}

int DisSignalPdu::get_body_length() const {
	const int64_t bytes = (data_length + 7) / 8;
	return static_cast<int>(BASE_SIZE + (bytes + 3) / 4 * 4);
}

PackedByteArray DisSignalPdu::encode() const {
	byte_io::Writer out;
	write_header(out);
	write_id(out, radio_reference_id);
	out.u16(radio_number);
	out.u16(encoding_scheme);
	out.u16(tdl_type);
	out.u32(static_cast<uint64_t>(sample_rate));
	out.u16(data_length);
	out.u16(samples);
	const int64_t bytes = (data_length + 7) / 8;
	out.raw(data.slice(0, bytes));
	out.zeros(bytes - std::min<int64_t>(bytes, data.size()));
	out.pad_from(BASE_SIZE, 4);
	return finish(out);
}

Ref<DisSignalPdu> DisSignalPdu::decode(const PackedByteArray &bytes) {
	if (!DisPdu::validate(bytes, DisPdu::PDU_TYPE_SIGNAL, BASE_SIZE)) {
		return Ref<DisSignalPdu>();
	}
	byte_io::Reader in(bytes, 0, DisPdu::get_effective_length(bytes));
	Ref<DisSignalPdu> out;
	out.instantiate();
	out->read_header(in);
	out->radio_reference_id = read_id(in);
	out->radio_number = static_cast<int>(in.u16());
	out->encoding_scheme = static_cast<int>(in.u16());
	out->tdl_type = static_cast<int>(in.u16());
	out->sample_rate = static_cast<int64_t>(in.u32());
	out->data_length = static_cast<int>(in.u16());
	out->samples = static_cast<int>(in.u16());
	out->data = in.raw((out->data_length + 7) / 8);
	if (!in.ok()) {
		DisPdu::set_last_error(vformat("%d bits of signal data do not fit in the PDU", out->data_length));
		return Ref<DisSignalPdu>();
	}
	out->keep_trailing(bytes);
	return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Receiver

void DisReceiverPdu::_bind_methods() {
	ClassDB::bind_static_method("DisReceiverPdu", D_METHOD("decode", "bytes"), &DisReceiverPdu::decode);
	ADD_GROUP("Receiver", "");
	DIS_BIND_PROPERTY(DisReceiverPdu, VECTOR3I, radio_reference_id);
	DIS_BIND_PROPERTY(DisReceiverPdu, INT, radio_number);
	DIS_BIND_PROPERTY(DisReceiverPdu, INT, receiver_state);
	DIS_BIND_PROPERTY(DisReceiverPdu, FLOAT, received_power);
	DIS_BIND_PROPERTY(DisReceiverPdu, VECTOR3I, transmitter_entity_id);
	DIS_BIND_PROPERTY(DisReceiverPdu, INT, transmitter_radio_number);
	ADD_GROUP("", "");

	BIND_ENUM_CONSTANT(RECEIVER_OFF);
	BIND_ENUM_CONSTANT(RECEIVER_ON_NOT_RECEIVING);
	BIND_ENUM_CONSTANT(RECEIVER_ON_AND_RECEIVING);
}

PackedByteArray DisReceiverPdu::encode() const {
	byte_io::Writer out;
	write_header(out);
	write_id(out, radio_reference_id);
	out.u16(radio_number);
	out.u16(receiver_state);
	out.u16(0); // padding
	out.f32(received_power);
	write_id(out, transmitter_entity_id);
	out.u16(transmitter_radio_number);
	return finish(out);
}

Ref<DisReceiverPdu> DisReceiverPdu::decode(const PackedByteArray &bytes) {
	if (!DisPdu::validate(bytes, DisPdu::PDU_TYPE_RECEIVER, BASE_SIZE)) {
		return Ref<DisReceiverPdu>();
	}
	byte_io::Reader in(bytes, 0, DisPdu::get_effective_length(bytes));
	Ref<DisReceiverPdu> out;
	out.instantiate();
	out->read_header(in);
	out->radio_reference_id = read_id(in);
	out->radio_number = static_cast<int>(in.u16());
	out->receiver_state = static_cast<int>(in.u16());
	in.skip(2);
	out->received_power = in.f32();
	out->transmitter_entity_id = read_id(in);
	out->transmitter_radio_number = static_cast<int>(in.u16());
	out->keep_trailing(bytes);
	return out;
}
