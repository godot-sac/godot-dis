#include "dis_pdu.h"

#include <cmath>
#include <string>

#include <godot_cpp/core/class_db.hpp>

#include "dis_entity_state_pdu.h"
#include "dis_simulation_management_pdus.h"
#include "dis_warfare_pdus.h"

using namespace godot;

namespace {
// thread_local: each thread gets its own copy, so a receive thread and the main thread
// don't overwrite each other's error message.
// std::string, not godot::String: globals are constructed when the DLL loads, before godot-cpp is
// initialized, and constructing a godot::String then calls into Godot and crashes the load.
thread_local std::string last_error;

constexpr double TIMESTAMP_UNITS_PER_HOUR = 2147483648.0; // 2^31

uint16_t read_u16_be(const PackedByteArray &bytes, int64_t offset) {
	return static_cast<uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}

uint32_t read_u32_be(const PackedByteArray &bytes, int64_t offset) {
	return (static_cast<uint32_t>(bytes[offset]) << 24) | (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
			(static_cast<uint32_t>(bytes[offset + 2]) << 8) | static_cast<uint32_t>(bytes[offset + 3]);
}
} // namespace

void DisPdu::_bind_methods() {
	ClassDB::bind_static_method("DisPdu", D_METHOD("peek_header", "bytes"), &DisPdu::peek_header);
	ClassDB::bind_static_method("DisPdu", D_METHOD("decode", "bytes"), &DisPdu::decode);
	ClassDB::bind_static_method("DisPdu", D_METHOD("is_supported", "pdu_type"), &DisPdu::is_supported);
	ClassDB::bind_static_method("DisPdu", D_METHOD("get_last_error"), &DisPdu::get_last_error);
	ClassDB::bind_static_method("DisPdu", D_METHOD("timestamp_from_seconds", "seconds_past_hour", "absolute"),
			&DisPdu::timestamp_from_seconds);
	ClassDB::bind_static_method("DisPdu", D_METHOD("timestamp_to_seconds", "timestamp"), &DisPdu::timestamp_to_seconds);
	ClassDB::bind_static_method("DisPdu", D_METHOD("timestamp_is_absolute", "timestamp"), &DisPdu::timestamp_is_absolute);

	BIND_ENUM_CONSTANT(PDU_TYPE_OTHER);
	BIND_ENUM_CONSTANT(PDU_TYPE_ENTITY_STATE);
	BIND_ENUM_CONSTANT(PDU_TYPE_FIRE);
	BIND_ENUM_CONSTANT(PDU_TYPE_DETONATION);
	BIND_ENUM_CONSTANT(PDU_TYPE_COLLISION);
	BIND_ENUM_CONSTANT(PDU_TYPE_CREATE_ENTITY);
	BIND_ENUM_CONSTANT(PDU_TYPE_REMOVE_ENTITY);
	BIND_ENUM_CONSTANT(PDU_TYPE_START_RESUME);
	BIND_ENUM_CONSTANT(PDU_TYPE_STOP_FREEZE);
	BIND_ENUM_CONSTANT(PDU_TYPE_ACKNOWLEDGE);
	BIND_ENUM_CONSTANT(PDU_TYPE_COMMENT);
}

Dictionary DisPdu::peek_header(const PackedByteArray &bytes) {
	Dictionary header;
	if (bytes.size() < HEADER_SIZE) {
		return header;
	}
	header["protocol_version"] = bytes[0];
	header["exercise_id"] = bytes[1];
	header["pdu_type"] = bytes[2];
	header["protocol_family"] = bytes[3];
	header["timestamp"] = static_cast<int64_t>(read_u32_be(bytes, 4));
	header["length"] = read_u16_be(bytes, 8);
	header["pdu_status"] = bytes[10];
	return header;
}

Ref<DisPduBase> DisPdu::decode(const PackedByteArray &bytes) {
	if (bytes.size() < HEADER_SIZE) {
		set_last_error(vformat("packet too short for a PDU header: %d bytes", bytes.size()));
		return Ref<DisPduBase>();
	}
	switch (bytes[2]) {
		case PDU_TYPE_ENTITY_STATE:
			return DisEntityStatePdu::decode(bytes);
		case PDU_TYPE_FIRE:
			return DisFirePdu::decode(bytes);
		case PDU_TYPE_DETONATION:
			return DisDetonationPdu::decode(bytes);
		case PDU_TYPE_REMOVE_ENTITY:
			return DisRemoveEntityPdu::decode(bytes);
		case PDU_TYPE_START_RESUME:
			return DisStartResumePdu::decode(bytes);
		case PDU_TYPE_STOP_FREEZE:
			return DisStopFreezePdu::decode(bytes);
		case PDU_TYPE_ACKNOWLEDGE:
			return DisAcknowledgePdu::decode(bytes);
		default:
			set_last_error(vformat("unsupported PDU type %d", bytes[2]));
			return Ref<DisPduBase>();
	}
}

bool DisPdu::is_supported(int type) {
	switch (type) {
		case PDU_TYPE_ENTITY_STATE:
		case PDU_TYPE_FIRE:
		case PDU_TYPE_DETONATION:
		case PDU_TYPE_REMOVE_ENTITY:
		case PDU_TYPE_START_RESUME:
		case PDU_TYPE_STOP_FREEZE:
		case PDU_TYPE_ACKNOWLEDGE:
			return true;
		default:
			return false;
	}
}

String DisPdu::get_last_error() {
	return String::utf8(last_error.c_str());
}

void DisPdu::set_last_error(const String &message) {
	last_error = message.utf8().get_data();
}

bool DisPdu::validate(const PackedByteArray &bytes, int expected_type, int minimum_size) {
	if (bytes.size() < HEADER_SIZE) {
		set_last_error(vformat("packet too short for a PDU header: %d bytes", bytes.size()));
		return false;
	}
	const int version = bytes[0];
	if (version < 1 || version > 7) {
		set_last_error(vformat("unknown protocol version %d", version));
		return false;
	}
	if (bytes[2] != expected_type) {
		set_last_error(vformat("expected PDU type %d, got %d", expected_type, bytes[2]));
		return false;
	}
	const int length = read_u16_be(bytes, 8);
	if (length > bytes.size()) {
		set_last_error(vformat("length field says %d bytes but only %d were received", length, bytes.size()));
		return false;
	}
	if (length < minimum_size) {
		set_last_error(vformat("PDU needs at least %d bytes, length field says %d", minimum_size, length));
		return false;
	}
	set_last_error("");
	return true;
}

int64_t DisPdu::timestamp_from_seconds(double seconds_past_hour, bool absolute) {
	double seconds = std::fmod(seconds_past_hour, 3600.0);
	if (seconds < 0.0) {
		seconds += 3600.0;
	}
	int64_t units = static_cast<int64_t>(seconds / 3600.0 * TIMESTAMP_UNITS_PER_HOUR);
	if (units > 0x7FFFFFFF) {
		units = 0x7FFFFFFF;
	}
	return (units << 1) | (absolute ? 1 : 0);
}

double DisPdu::timestamp_to_seconds(int64_t timestamp) {
	const int64_t units = (timestamp & 0xFFFFFFFF) >> 1;
	return static_cast<double>(units) / TIMESTAMP_UNITS_PER_HOUR * 3600.0;
}

bool DisPdu::timestamp_is_absolute(int64_t timestamp) {
	return (timestamp & 1) != 0;
}
