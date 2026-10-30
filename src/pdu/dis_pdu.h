#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

// Static helpers shared by all PDUs: header inspection, decoding by type, validation and timestamps.
//
// Every buffer that comes from the network is validated here before open-dis-cpp sees it:
// open-dis-cpp reads with std::vector::at(), and with exceptions disabled an out-of-range read
// would terminate Godot instead of failing gracefully.
class DisPdu : public godot::Object {
	GDCLASS(DisPdu, godot::Object)

protected:
	static void _bind_methods();

public:
	// PDU type numbers (SISO-REF-010 "DISPDUType").
	enum PduType {
		PDU_TYPE_OTHER = 0,
		PDU_TYPE_ENTITY_STATE = 1,
		PDU_TYPE_FIRE = 2,
		PDU_TYPE_DETONATION = 3,
	};

	static constexpr int HEADER_SIZE = 12;

	// Reads the 12-byte header without decoding the body. Returns {} if the buffer is too short.
	// Keys: protocol_version, exercise_id, pdu_type, protocol_family, timestamp, length, pdu_status.
	static godot::Dictionary peek_header(const godot::PackedByteArray &bytes);

	// Decodes any supported PDU (currently Entity State). Returns null and sets get_last_error() otherwise.
	static godot::Ref<godot::RefCounted> decode(const godot::PackedByteArray &bytes);

	// Reason the last decode() / DisEntityStatePdu.decode() call on this thread returned null.
	static godot::String get_last_error();

	// DIS timestamps count 2^31 units per hour, shifted left by one; the low bit marks absolute time.
	static int64_t timestamp_from_seconds(double seconds_past_hour, bool absolute);
	static double timestamp_to_seconds(int64_t timestamp);
	static bool timestamp_is_absolute(int64_t timestamp);

	// Internal: checks the header and that `bytes` holds at least `minimum_body` bytes of PDU.
	// On failure sets the last error and returns false.
	static bool validate(const godot::PackedByteArray &bytes, int expected_type, int minimum_size);
	static void set_last_error(const godot::String &message);
};

VARIANT_ENUM_CAST(DisPdu::PduType);
