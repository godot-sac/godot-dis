#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "dis_simulation_management_pdus.h"

// Simulation Management PDUs that carry datum records: Comment (type 22), Set Data (19) and Data (20).
// Layouts: docs/simulation-management.md. Encoded with our own code (open-dis-cpp's variable datum rounds bit
// lengths down to whole bytes).
//
// Fixed datum: {id, value} (32-bit ID, 32 raw bits of value). Variable datum: {id, length (bits), data
// (PackedByteArray, ceil(length / 8) bytes)}; on the wire the data is padded to a multiple of 8 bytes.
// IDs are SISO-REF-010 "Variable Record Types" (UID 66) or exercise-specific.
class DisDatumPdu : public DisSimulationManagementPdu {
	GDCLASS(DisDatumPdu, DisSimulationManagementPdu)

protected:
	godot::Array fixed_datums;
	godot::Array variable_datums;
	int64_t padding = 0; // Data / Set Data only

	static void _bind_methods();
	virtual bool has_request() const { return true; }
	// Shared decode: checks the type, reads everything into `out`. Null on malformed input.
	static bool decode_into(DisDatumPdu *out, const godot::PackedByteArray &bytes, int type);

public:
	godot::PackedByteArray encode() const override;
	int get_body_length() const override;

	void add_fixed_datum(int64_t id, int64_t value);
	// length_bits < 0: the whole of `data`.
	void add_variable_datum(int64_t id, const godot::PackedByteArray &data, int64_t length_bits);
	// A variable datum holding UTF-8 text, and the text of the first datum with `id` ("" if there is none).
	void add_text(int64_t id, const godot::String &text);
	godot::String get_text(int64_t id) const;

	void set_fixed_datums(const godot::Array &value) { fixed_datums = value; }
	godot::Array get_fixed_datums() const { return fixed_datums; }
	void set_variable_datums(const godot::Array &value) { variable_datums = value; }
	godot::Array get_variable_datums() const { return variable_datums; }
	void set_padding(int64_t value) { padding = value; }
	int64_t get_padding() const { return padding; }
};

// Comment PDU (type 22): free text (and other data) for logging or display; no request ID.
class DisCommentPdu : public DisDatumPdu {
	GDCLASS(DisCommentPdu, DisDatumPdu)

protected:
	static void _bind_methods();
	bool has_request() const override { return false; }

public:
	static godot::Ref<DisCommentPdu> decode(const godot::PackedByteArray &bytes);
	int get_pdu_type() const override { return 22; }
};

// Set Data PDU (type 19): asks the receiver to set the given values.
class DisSetDataPdu : public DisDatumPdu {
	GDCLASS(DisSetDataPdu, DisDatumPdu)

protected:
	static void _bind_methods();

public:
	static godot::Ref<DisSetDataPdu> decode(const godot::PackedByteArray &bytes);
	int get_pdu_type() const override { return 19; }
};

// Data PDU (type 20): values, as an answer to Data Query / Set Data or unsolicited.
class DisDataPdu : public DisDatumPdu {
	GDCLASS(DisDataPdu, DisDatumPdu)

protected:
	static void _bind_methods();

public:
	static godot::Ref<DisDataPdu> decode(const godot::PackedByteArray &bytes);
	int get_pdu_type() const override { return 20; }
};
