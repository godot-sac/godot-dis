#pragma once

#include <dis7/utils/DataStream.h>

#include <godot_cpp/variant/packed_byte_array.hpp>

// Copies between Godot byte arrays and open-dis-cpp DataStreams (always big-endian, as on the wire).
namespace pdu_bytes {

inline DIS::DataStream to_stream(const godot::PackedByteArray &bytes) {
	return DIS::DataStream(reinterpret_cast<const char *>(bytes.ptr()), static_cast<size_t>(bytes.size()), DIS::BIG);
}

inline godot::PackedByteArray from_stream(const DIS::DataStream &stream) {
	godot::PackedByteArray bytes;
	bytes.resize(static_cast<int64_t>(stream.size()));
	for (size_t i = 0; i < stream.size(); i++) {
		bytes[static_cast<int64_t>(i)] = static_cast<uint8_t>(stream[static_cast<unsigned int>(i)]);
	}
	return bytes;
}

} // namespace pdu_bytes
