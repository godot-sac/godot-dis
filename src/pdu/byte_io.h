#pragma once

#include <cstdint>
#include <cstring>

#include <godot_cpp/variant/packed_byte_array.hpp>

// Big-endian readers and writers for the PDUs we encode ourselves (where open-dis-cpp has no DIS 7 class or
// mishandles variable-length records): Comment, Data, Set Data, Transmitter, Signal.
namespace byte_io {

// Appends to a PackedByteArray.
class Writer {
	godot::PackedByteArray bytes;

public:
	void u8(uint64_t v) { bytes.push_back(static_cast<uint8_t>(v)); }
	void u16(uint64_t v) {
		u8(v >> 8);
		u8(v);
	}
	void u32(uint64_t v) {
		u16(v >> 16);
		u16(v);
	}
	void u64(uint64_t v) {
		u32(v >> 32);
		u32(v);
	}
	void f32(double v) {
		const float f = static_cast<float>(v);
		uint32_t bits;
		std::memcpy(&bits, &f, 4);
		u32(bits);
	}
	void f64(double v) {
		uint64_t bits;
		std::memcpy(&bits, &v, 8);
		u64(bits);
	}
	void raw(const godot::PackedByteArray &data) { bytes.append_array(data); }
	void zeros(int64_t count) {
		for (int64_t i = 0; i < count; i++) {
			u8(0);
		}
	}
	// Pads with zeros to a multiple of `boundary` bytes counted from `from`.
	void pad_from(int64_t from, int boundary) {
		while ((bytes.size() - from) % boundary != 0) {
			u8(0);
		}
	}
	int64_t size() const { return bytes.size(); }
	// Writes the total length into the header (bytes 8-9) and returns the bytes.
	godot::PackedByteArray finish() {
		const int64_t length = bytes.size();
		bytes.set(8, static_cast<uint8_t>(length >> 8));
		bytes.set(9, static_cast<uint8_t>(length));
		return bytes;
	}
};

// Reads from a PackedByteArray without ever reading past `end`; after a failed read ok() is false and every
// further read returns 0.
class Reader {
	const godot::PackedByteArray &bytes;
	int64_t at;
	int64_t end;
	bool good = true;

	bool take(int64_t count) {
		if (!good || count < 0 || at + count > end) {
			good = false;
			return false;
		}
		return true;
	}

public:
	Reader(const godot::PackedByteArray &p_bytes, int64_t p_start, int64_t p_end) :
			bytes(p_bytes), at(p_start), end(p_end) {}

	bool ok() const { return good; }
	int64_t position() const { return at; }
	int64_t remaining() const { return good ? end - at : 0; }

	uint64_t u8() {
		if (!take(1)) {
			return 0;
		}
		return bytes[at++];
	}
	uint64_t u16() {
		const uint64_t high = u8();
		return (high << 8) | u8();
	}
	uint64_t u32() {
		const uint64_t high = u16();
		return (high << 16) | u16();
	}
	uint64_t u64() {
		const uint64_t high = u32();
		return (high << 32) | u32();
	}
	double f32() {
		const uint32_t bits = static_cast<uint32_t>(u32());
		float f;
		std::memcpy(&f, &bits, 4);
		return f;
	}
	double f64() {
		const uint64_t bits = u64();
		double d;
		std::memcpy(&d, &bits, 8);
		return d;
	}
	godot::PackedByteArray raw(int64_t count) {
		if (!take(count)) {
			return godot::PackedByteArray();
		}
		godot::PackedByteArray out = bytes.slice(at, at + count);
		at += count;
		return out;
	}
	void skip(int64_t count) {
		if (take(count)) {
			at += count;
		}
	}
};

} // namespace byte_io
