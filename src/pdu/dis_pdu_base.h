#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include "pdu/byte_io.h"

namespace DIS {
class Pdu;
}

// Binds get_<name>/set_<name> of `cls` and registers the property, to keep the binding lists readable.
#define DIS_BIND_PROPERTY(cls, variant_type, name)                                 \
	godot::ClassDB::bind_method(godot::D_METHOD("set_" #name, "value"), &cls::set_##name); \
	godot::ClassDB::bind_method(godot::D_METHOD("get_" #name), &cls::get_##name);          \
	ADD_PROPERTY(godot::PropertyInfo(godot::Variant::variant_type, #name), "set_" #name, "get_" #name)

// Base class of every PDU object: the header fields every PDU has and the common API.
// Subclasses (DisEntityStatePdu, DisFirePdu, ...) add the body fields and implement encode() and a static decode().
class DisPduBase : public godot::RefCounted {
	GDCLASS(DisPduBase, godot::RefCounted)

protected:
	int protocol_version = 7;
	int exercise_id = 1;
	int64_t timestamp = 0;
	int pdu_status = 0;
	// The protocol family byte as received, if it differs from get_protocol_family() (older senders write 0);
	// written back unchanged so decode + encode gives the same bytes. -1 = use get_protocol_family().
	int received_family = -1;
	// Bytes after the last field we decode but inside the header length (vendor extensions, a sender's extra
	// padding); written back by encode() so decode + encode gives the same bytes.
	godot::PackedByteArray trailing_bytes;
	int header_family() const { return received_family >= 0 ? received_family : get_protocol_family(); }

	static void _bind_methods();

	// Header fields to and from an open-dis-cpp PDU (type and family are set by its constructor).
	void write_header(DIS::Pdu &pdu) const;
	void read_header(const DIS::Pdu &pdu);
	// Sets the length field (open-dis-cpp's marshal() doesn't) and serializes, big-endian.
	godot::PackedByteArray marshal(DIS::Pdu &pdu) const;
	// The same 12-byte header for PDUs we encode ourselves (byte_io); the length is filled in by Writer::finish().
	void write_header(byte_io::Writer &out) const;
	void read_header(byte_io::Reader &in);
	// Appends trailing_bytes and fills in the length.
	godot::PackedByteArray finish(byte_io::Writer &out) const;
	// After decoding: keeps the bytes between get_body_length() and the header length as trailing_bytes.
	void keep_trailing(const godot::PackedByteArray &bytes);

public:
	// Serializes to DIS bytes (big-endian), with the header length filled in.
	virtual godot::PackedByteArray encode() const { return godot::PackedByteArray(); }
	// PDU type number (SISO-REF-010 "DIS-PDU Type", see DisPdu.PduType).
	virtual int get_pdu_type() const { return 0; }
	// Protocol family number (SISO-REF-010 "DIS-Protocol Family").
	virtual int get_protocol_family() const { return 0; }
	// Encoded size in bytes of the fields this class knows (without trailing_bytes).
	virtual int get_body_length() const { return 12; }
	// Encoded size in bytes: get_body_length() + trailing_bytes.
	int get_length() const { return get_body_length() + static_cast<int>(trailing_bytes.size()); }

	void set_protocol_version(int value) { protocol_version = value; }
	int get_protocol_version() const { return protocol_version; }
	void set_exercise_id(int value) { exercise_id = value; }
	int get_exercise_id() const { return exercise_id; }
	void set_timestamp(int64_t value) { timestamp = value; }
	int64_t get_timestamp() const { return timestamp; }
	void set_pdu_status(int value) { pdu_status = value; }
	int get_pdu_status() const { return pdu_status; }
	void set_trailing_bytes(const godot::PackedByteArray &value) { trailing_bytes = value; }
	godot::PackedByteArray get_trailing_bytes() const { return trailing_bytes; }
};
