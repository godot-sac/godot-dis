#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include "dis_pdu_base.h"

// Radio Communications family (protocol family 4): who transmits on which frequency (Transmitter), the
// transmitted data (Signal) and who receives it (Receiver). Encoded with our own code (open-dis-cpp has no DIS 7 Transmitter PDU). godot-dis
// decodes them and shows who is on the air; it does not play or record audio. Layouts: docs/radio.md.

// Transmitter PDU (type 25): the state of one radio. 104 bytes plus the variable-length records.
class DisTransmitterPdu : public DisPduBase {
	GDCLASS(DisTransmitterPdu, DisPduBase)

	godot::Vector3i radio_reference_id; // the entity (or object) the radio belongs to
	int radio_number = 0;
	godot::PackedInt32Array radio_type; // kind, domain, country, category, nomenclature version, nomenclature
	int transmit_state = 0;
	int input_source = 0;
	godot::PackedFloat64Array antenna_location_ecef;
	godot::Vector3 relative_antenna_location;
	int antenna_pattern_type = 0;
	int64_t frequency = 0; // Hz
	double bandwidth = 0.0; // Hz
	double power = 0.0; // dBm
	godot::PackedInt32Array modulation_type; // spread spectrum, major, detail, radio system
	int crypto_system = 0;
	int crypto_key_id = 0;
	godot::PackedByteArray modulation_parameters; // raw
	godot::PackedByteArray antenna_pattern; // raw
	godot::Array variable_parameters; // raw records: type (4), length (2), data, each a PackedByteArray

protected:
	static void _bind_methods();

public:
	static constexpr int BASE_SIZE = 104;

	// SISO-REF-010 "Transmitter-Transmit State" (UID 164).
	enum TransmitState {
		TRANSMIT_OFF = 0,
		TRANSMIT_ON_NOT_TRANSMITTING = 1,
		TRANSMIT_ON_AND_TRANSMITTING = 2,
	};

	DisTransmitterPdu();

	godot::PackedByteArray encode() const override;
	static godot::Ref<DisTransmitterPdu> decode(const godot::PackedByteArray &bytes);
	int get_pdu_type() const override { return 25; }
	int get_protocol_family() const override { return 4; }
	int get_body_length() const override;

	void set_radio_reference_id(const godot::Vector3i &value) { radio_reference_id = value; }
	godot::Vector3i get_radio_reference_id() const { return radio_reference_id; }
	void set_radio_number(int value) { radio_number = value; }
	int get_radio_number() const { return radio_number; }
	void set_radio_type(const godot::PackedInt32Array &value);
	godot::PackedInt32Array get_radio_type() const { return radio_type; }
	void set_transmit_state(int value) { transmit_state = value; }
	int get_transmit_state() const { return transmit_state; }
	void set_input_source(int value) { input_source = value; }
	int get_input_source() const { return input_source; }
	void set_antenna_location_ecef(const godot::PackedFloat64Array &value);
	godot::PackedFloat64Array get_antenna_location_ecef() const { return antenna_location_ecef; }
	void set_relative_antenna_location(const godot::Vector3 &value) { relative_antenna_location = value; }
	godot::Vector3 get_relative_antenna_location() const { return relative_antenna_location; }
	void set_antenna_pattern_type(int value) { antenna_pattern_type = value; }
	int get_antenna_pattern_type() const { return antenna_pattern_type; }
	void set_frequency(int64_t value) { frequency = value; }
	int64_t get_frequency() const { return frequency; }
	void set_bandwidth(double value) { bandwidth = value; }
	double get_bandwidth() const { return bandwidth; }
	void set_power(double value) { power = value; }
	double get_power() const { return power; }
	void set_modulation_type(const godot::PackedInt32Array &value);
	godot::PackedInt32Array get_modulation_type() const { return modulation_type; }
	void set_crypto_system(int value) { crypto_system = value; }
	int get_crypto_system() const { return crypto_system; }
	void set_crypto_key_id(int value) { crypto_key_id = value; }
	int get_crypto_key_id() const { return crypto_key_id; }
	void set_modulation_parameters(const godot::PackedByteArray &value) { modulation_parameters = value; }
	godot::PackedByteArray get_modulation_parameters() const { return modulation_parameters; }
	void set_antenna_pattern(const godot::PackedByteArray &value) { antenna_pattern = value; }
	godot::PackedByteArray get_antenna_pattern() const { return antenna_pattern; }
	void set_variable_parameters(const godot::Array &value) { variable_parameters = value; }
	godot::Array get_variable_parameters() const { return variable_parameters; }
};

// Signal PDU (type 26): a block of transmitted data (encoded audio, tactical data link messages ...).
// 32 bytes plus the data, padded to a multiple of 4 bytes.
class DisSignalPdu : public DisPduBase {
	GDCLASS(DisSignalPdu, DisPduBase)

	godot::Vector3i radio_reference_id;
	int radio_number = 0;
	int encoding_scheme = 0; // bits 14-15 class, 0-13 type (SISO-REF-010 UID 270, 271)
	int tdl_type = 0;
	int64_t sample_rate = 0;
	int data_length = 0; // bits
	int samples = 0;
	godot::PackedByteArray data; // ceil(data_length / 8) bytes

protected:
	static void _bind_methods();

public:
	static constexpr int BASE_SIZE = 32;

	godot::PackedByteArray encode() const override;
	static godot::Ref<DisSignalPdu> decode(const godot::PackedByteArray &bytes);
	int get_pdu_type() const override { return 26; }
	int get_protocol_family() const override { return 4; }
	int get_body_length() const override;

	// Sets data and data_length (whole bytes).
	void set_data_bytes(const godot::PackedByteArray &value);

	void set_radio_reference_id(const godot::Vector3i &value) { radio_reference_id = value; }
	godot::Vector3i get_radio_reference_id() const { return radio_reference_id; }
	void set_radio_number(int value) { radio_number = value; }
	int get_radio_number() const { return radio_number; }
	void set_encoding_scheme(int value) { encoding_scheme = value; }
	int get_encoding_scheme() const { return encoding_scheme; }
	void set_tdl_type(int value) { tdl_type = value; }
	int get_tdl_type() const { return tdl_type; }
	void set_sample_rate(int64_t value) { sample_rate = value; }
	int64_t get_sample_rate() const { return sample_rate; }
	void set_data_length(int value) { data_length = value; }
	int get_data_length() const { return data_length; }
	void set_samples(int value) { samples = value; }
	int get_samples() const { return samples; }
	void set_data(const godot::PackedByteArray &value) { data = value; }
	godot::PackedByteArray get_data() const { return data; }
};

// Receiver PDU (type 27): the state of one radio receiver and the transmitter it last heard. 36 bytes.
class DisReceiverPdu : public DisPduBase {
	GDCLASS(DisReceiverPdu, DisPduBase)

	godot::Vector3i radio_reference_id; // the entity (or object) the receiver belongs to
	int radio_number = 0;
	int receiver_state = 0;
	double received_power = 0.0; // dBm
	godot::Vector3i transmitter_entity_id;
	int transmitter_radio_number = 0;

protected:
	static void _bind_methods();

public:
	static constexpr int BASE_SIZE = 36;

	// SISO-REF-010 "Receiver-Receiver State" (UID 179).
	enum ReceiverState {
		RECEIVER_OFF = 0,
		RECEIVER_ON_NOT_RECEIVING = 1,
		RECEIVER_ON_AND_RECEIVING = 2,
	};

	godot::PackedByteArray encode() const override;
	static godot::Ref<DisReceiverPdu> decode(const godot::PackedByteArray &bytes);
	int get_pdu_type() const override { return 27; }
	int get_protocol_family() const override { return 4; }
	int get_body_length() const override { return BASE_SIZE; }

	void set_radio_reference_id(const godot::Vector3i &value) { radio_reference_id = value; }
	godot::Vector3i get_radio_reference_id() const { return radio_reference_id; }
	void set_radio_number(int value) { radio_number = value; }
	int get_radio_number() const { return radio_number; }
	void set_receiver_state(int value) { receiver_state = value; }
	int get_receiver_state() const { return receiver_state; }
	void set_received_power(double value) { received_power = value; }
	double get_received_power() const { return received_power; }
	void set_transmitter_entity_id(const godot::Vector3i &value) { transmitter_entity_id = value; }
	godot::Vector3i get_transmitter_entity_id() const { return transmitter_entity_id; }
	void set_transmitter_radio_number(int value) { transmitter_radio_number = value; }
	int get_transmitter_radio_number() const { return transmitter_radio_number; }
};

VARIANT_ENUM_CAST(DisTransmitterPdu::TransmitState);
VARIANT_ENUM_CAST(DisReceiverPdu::ReceiverState);
