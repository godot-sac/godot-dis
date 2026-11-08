#include "dis_emission_pdus.h"

#include <godot_cpp/core/class_db.hpp>

#include "byte_io.h"
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

void write_vector(byte_io::Writer &out, const Vector3 &v) {
	out.f32(v.x);
	out.f32(v.y);
	out.f32(v.z);
}

Vector3 read_vector(byte_io::Reader &in) {
	const double x = in.f32();
	const double y = in.f32();
	return Vector3(x, y, in.f32());
}

// Length fields in 32-bit words; 0 when the record is longer than 255 words (the standard's rule).
int words_or_zero(int64_t bytes) {
	const int64_t words = bytes / 4;
	return words > 255 ? 0 : static_cast<int>(words);
}

const char *const BEAM_FLOATS[] = { "frequency", "frequency_range", "effective_radiated_power",
	"pulse_repetition_frequency", "pulse_width", "azimuth_center", "azimuth_sweep", "elevation_center",
	"elevation_sweep", "sweep_sync" };
} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Electromagnetic Emission

void DisElectromagneticEmissionPdu::_bind_methods() {
	ClassDB::bind_static_method("DisElectromagneticEmissionPdu", D_METHOD("decode", "bytes"),
			&DisElectromagneticEmissionPdu::decode);
	ClassDB::bind_static_method("DisElectromagneticEmissionPdu", D_METHOD("make_system"),
			&DisElectromagneticEmissionPdu::make_system);
	ClassDB::bind_static_method("DisElectromagneticEmissionPdu", D_METHOD("make_beam"),
			&DisElectromagneticEmissionPdu::make_beam);
	ADD_GROUP("Electromagnetic Emission", "");
	DIS_BIND_PROPERTY(DisElectromagneticEmissionPdu, VECTOR3I, emitting_entity_id);
	DIS_BIND_PROPERTY(DisElectromagneticEmissionPdu, VECTOR3I, event_id);
	DIS_BIND_PROPERTY(DisElectromagneticEmissionPdu, INT, state_update_indicator);
	DIS_BIND_PROPERTY(DisElectromagneticEmissionPdu, ARRAY, systems);
	ADD_GROUP("", "");

	BIND_ENUM_CONSTANT(STATE_HEARTBEAT);
	BIND_ENUM_CONSTANT(STATE_CHANGED_DATA);
}

Dictionary DisElectromagneticEmissionPdu::make_system() {
	Dictionary system;
	system["emitter_name"] = 0;
	system["emitter_function"] = 0;
	system["emitter_number"] = 0;
	system["location"] = Vector3();
	system["beams"] = Array();
	return system;
}

Dictionary DisElectromagneticEmissionPdu::make_beam() {
	Dictionary beam;
	beam["beam_id"] = 0;
	beam["parameter_index"] = 0;
	for (const char *key : BEAM_FLOATS) {
		beam[key] = 0.0;
	}
	beam["function"] = 0;
	beam["high_density_track_jam"] = 0;
	beam["status"] = 0;
	beam["jamming_technique"] = 0;
	beam["track_jam"] = Array();
	return beam;
}

int DisElectromagneticEmissionPdu::get_body_length() const {
	int64_t length = BASE_SIZE;
	for (int64_t s = 0; s < systems.size(); s++) {
		const Dictionary system = systems[s];
		const Array beams = system.get("beams", Array());
		length += SYSTEM_SIZE;
		for (int64_t b = 0; b < beams.size(); b++) {
			const Dictionary beam = beams[b];
			length += BEAM_SIZE + TRACK_JAM_SIZE * Array(beam.get("track_jam", Array())).size();
		}
	}
	return static_cast<int>(length);
}

PackedByteArray DisElectromagneticEmissionPdu::encode() const {
	ERR_FAIL_COND_V_MSG(systems.size() > 255, PackedByteArray(), "at most 255 emitter systems per PDU");
	byte_io::Writer out;
	write_header(out);
	write_id(out, emitting_entity_id);
	write_id(out, event_id);
	out.u8(state_update_indicator);
	out.u8(systems.size());
	out.u16(0); // padding
	for (int64_t s = 0; s < systems.size(); s++) {
		ERR_FAIL_COND_V_MSG(systems[s].get_type() != Variant::DICTIONARY, PackedByteArray(),
				vformat("system %d is not a Dictionary", s));
		const Dictionary system = systems[s];
		const Array beams = system.get("beams", Array());
		ERR_FAIL_COND_V_MSG(beams.size() > 255, PackedByteArray(), "at most 255 beams per emitter system");
		int64_t system_bytes = SYSTEM_SIZE;
		for (int64_t b = 0; b < beams.size(); b++) {
			ERR_FAIL_COND_V_MSG(beams[b].get_type() != Variant::DICTIONARY, PackedByteArray(),
					vformat("system %d beam %d is not a Dictionary", s, b));
			const Array targets = Dictionary(beams[b]).get("track_jam", Array());
			ERR_FAIL_COND_V_MSG(targets.size() > 255, PackedByteArray(), "at most 255 track / jam targets per beam");
			system_bytes += BEAM_SIZE + TRACK_JAM_SIZE * targets.size();
		}
		out.u8(words_or_zero(system_bytes));
		out.u8(beams.size());
		out.u16(0); // padding
		out.u16(int(system.get("emitter_name", 0)));
		out.u8(int(system.get("emitter_function", 0)));
		out.u8(int(system.get("emitter_number", 0)));
		write_vector(out, system.get("location", Vector3()));
		for (int64_t b = 0; b < beams.size(); b++) {
			const Dictionary beam = beams[b];
			const Array targets = beam.get("track_jam", Array());
			out.u8(words_or_zero(BEAM_SIZE + TRACK_JAM_SIZE * targets.size()));
			out.u8(int(beam.get("beam_id", 0)));
			out.u16(int(beam.get("parameter_index", 0)));
			for (const char *key : BEAM_FLOATS) {
				out.f32(double(beam.get(key, 0.0)));
			}
			out.u8(int(beam.get("function", 0)));
			out.u8(targets.size());
			out.u8(int(beam.get("high_density_track_jam", 0)));
			out.u8(int(beam.get("status", 0)));
			out.u32(static_cast<uint64_t>(int64_t(beam.get("jamming_technique", 0))) & 0xFFFFFFFF);
			for (int64_t t = 0; t < targets.size(); t++) {
				ERR_FAIL_COND_V_MSG(targets[t].get_type() != Variant::DICTIONARY, PackedByteArray(),
						vformat("system %d beam %d target %d is not a Dictionary", s, b, t));
				const Dictionary target = targets[t];
				write_id(out, target.get("entity_id", Vector3i()));
				out.u8(int(target.get("emitter_number", 0)));
				out.u8(int(target.get("beam_number", 0)));
			}
		}
	}
	return finish(out);
}

Ref<DisElectromagneticEmissionPdu> DisElectromagneticEmissionPdu::decode(const PackedByteArray &bytes) {
	if (!DisPdu::validate(bytes, DisPdu::PDU_TYPE_ELECTROMAGNETIC_EMISSION, BASE_SIZE)) {
		return Ref<DisElectromagneticEmissionPdu>();
	}
	byte_io::Reader in(bytes, 0, DisPdu::get_effective_length(bytes));
	Ref<DisElectromagneticEmissionPdu> out;
	out.instantiate();
	out->read_header(in);
	out->emitting_entity_id = read_id(in);
	out->event_id = read_id(in);
	out->state_update_indicator = static_cast<int>(in.u8());
	const int system_count = static_cast<int>(in.u8());
	in.skip(2);
	// The counts drive the parsing; the length fields (in words, 0 when long) are only written.
	for (int s = 0; s < system_count && in.ok(); s++) {
		in.skip(1);
		const int beam_count = static_cast<int>(in.u8());
		in.skip(2);
		Dictionary system = make_system();
		system["emitter_name"] = static_cast<int>(in.u16());
		system["emitter_function"] = static_cast<int>(in.u8());
		system["emitter_number"] = static_cast<int>(in.u8());
		system["location"] = read_vector(in);
		Array beams;
		for (int b = 0; b < beam_count && in.ok(); b++) {
			in.skip(1);
			Dictionary beam = make_beam();
			beam["beam_id"] = static_cast<int>(in.u8());
			beam["parameter_index"] = static_cast<int>(in.u16());
			for (const char *key : BEAM_FLOATS) {
				beam[key] = in.f32();
			}
			beam["function"] = static_cast<int>(in.u8());
			const int target_count = static_cast<int>(in.u8());
			beam["high_density_track_jam"] = static_cast<int>(in.u8());
			beam["status"] = static_cast<int>(in.u8());
			beam["jamming_technique"] = static_cast<int64_t>(in.u32());
			Array targets;
			for (int t = 0; t < target_count && in.ok(); t++) {
				Dictionary target;
				target["entity_id"] = read_id(in);
				target["emitter_number"] = static_cast<int>(in.u8());
				target["beam_number"] = static_cast<int>(in.u8());
				targets.push_back(target);
			}
			beam["track_jam"] = targets;
			beams.push_back(beam);
		}
		system["beams"] = beams;
		out->systems.push_back(system);
	}
	if (!in.ok()) {
		DisPdu::set_last_error("Electromagnetic Emission PDU: the system, beam and target counts run past its length");
		return Ref<DisElectromagneticEmissionPdu>();
	}
	out->keep_trailing(bytes);
	return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Designator

DisDesignatorPdu::DisDesignatorPdu() {
	spot_location_ecef = pdu_convert::zero_location();
}

void DisDesignatorPdu::_bind_methods() {
	ClassDB::bind_static_method("DisDesignatorPdu", D_METHOD("decode", "bytes"), &DisDesignatorPdu::decode);
	ADD_GROUP("Designator", "");
	DIS_BIND_PROPERTY(DisDesignatorPdu, VECTOR3I, designating_entity_id);
	DIS_BIND_PROPERTY(DisDesignatorPdu, INT, code_name);
	DIS_BIND_PROPERTY(DisDesignatorPdu, VECTOR3I, designated_entity_id);
	DIS_BIND_PROPERTY(DisDesignatorPdu, INT, designator_code);
	DIS_BIND_PROPERTY(DisDesignatorPdu, FLOAT, power);
	DIS_BIND_PROPERTY(DisDesignatorPdu, FLOAT, wavelength);
	DIS_BIND_PROPERTY(DisDesignatorPdu, VECTOR3, spot_offset);
	DIS_BIND_PROPERTY(DisDesignatorPdu, PACKED_FLOAT64_ARRAY, spot_location_ecef);
	DIS_BIND_PROPERTY(DisDesignatorPdu, INT, dr_algorithm);
	DIS_BIND_PROPERTY(DisDesignatorPdu, VECTOR3, spot_linear_acceleration);
	ADD_GROUP("", "");
}

void DisDesignatorPdu::set_spot_location_ecef(const PackedFloat64Array &value) {
	ERR_FAIL_COND_MSG(value.size() != 3, "spot_location_ecef needs 3 values (x, y, z)");
	spot_location_ecef = value;
}

PackedByteArray DisDesignatorPdu::encode() const {
	byte_io::Writer out;
	write_header(out);
	write_id(out, designating_entity_id);
	out.u16(code_name);
	write_id(out, designated_entity_id);
	out.u16(designator_code);
	out.f32(power);
	out.f32(wavelength);
	write_vector(out, spot_offset);
	for (int i = 0; i < 3; i++) {
		out.f64(spot_location_ecef[i]);
	}
	out.u8(dr_algorithm);
	out.zeros(3); // padding
	write_vector(out, spot_linear_acceleration);
	return finish(out);
}

Ref<DisDesignatorPdu> DisDesignatorPdu::decode(const PackedByteArray &bytes) {
	if (!DisPdu::validate(bytes, DisPdu::PDU_TYPE_DESIGNATOR, BASE_SIZE)) {
		return Ref<DisDesignatorPdu>();
	}
	byte_io::Reader in(bytes, 0, DisPdu::get_effective_length(bytes));
	Ref<DisDesignatorPdu> out;
	out.instantiate();
	out->read_header(in);
	out->designating_entity_id = read_id(in);
	out->code_name = static_cast<int>(in.u16());
	out->designated_entity_id = read_id(in);
	out->designator_code = static_cast<int>(in.u16());
	out->power = in.f32();
	out->wavelength = in.f32();
	out->spot_offset = read_vector(in);
	for (int i = 0; i < 3; i++) {
		out->spot_location_ecef.set(i, in.f64());
	}
	out->dr_algorithm = static_cast<int>(in.u8());
	in.skip(3);
	out->spot_linear_acceleration = read_vector(in);
	out->keep_trailing(bytes);
	return out;
}

// ---------------------------------------------------------------------------------------------------------------
// IFF

DisIffPdu::DisIffPdu() {
	parameters = pdu_convert::zeros(6);
}

void DisIffPdu::_bind_methods() {
	ClassDB::bind_static_method("DisIffPdu", D_METHOD("decode", "bytes"), &DisIffPdu::decode);
	ADD_GROUP("IFF", "");
	DIS_BIND_PROPERTY(DisIffPdu, VECTOR3I, emitting_entity_id);
	DIS_BIND_PROPERTY(DisIffPdu, VECTOR3I, event_id);
	DIS_BIND_PROPERTY(DisIffPdu, VECTOR3, relative_antenna_location);
	DIS_BIND_PROPERTY(DisIffPdu, INT, system_type);
	DIS_BIND_PROPERTY(DisIffPdu, INT, system_name);
	DIS_BIND_PROPERTY(DisIffPdu, INT, system_mode);
	DIS_BIND_PROPERTY(DisIffPdu, INT, change_options);
	DIS_BIND_PROPERTY(DisIffPdu, INT, system_designator);
	DIS_BIND_PROPERTY(DisIffPdu, INT, system_specific_data);
	DIS_BIND_PROPERTY(DisIffPdu, INT, system_status);
	DIS_BIND_PROPERTY(DisIffPdu, INT, data_field_1);
	DIS_BIND_PROPERTY(DisIffPdu, INT, information_layers);
	DIS_BIND_PROPERTY(DisIffPdu, INT, data_field_2);
	DIS_BIND_PROPERTY(DisIffPdu, PACKED_INT32_ARRAY, parameters);
	DIS_BIND_PROPERTY(DisIffPdu, ARRAY, layers);
	ADD_GROUP("", "");
}

void DisIffPdu::set_parameters(const PackedInt32Array &value) {
	ERR_FAIL_COND_MSG(value.size() != 6, "parameters needs 6 values (parameters 1-6)");
	parameters = value;
}

int DisIffPdu::get_body_length() const {
	int64_t length = BASE_SIZE;
	for (int64_t i = 0; i < layers.size(); i++) {
		length += PackedByteArray(layers[i]).size();
	}
	return static_cast<int>(length);
}

PackedByteArray DisIffPdu::encode() const {
	for (int64_t i = 0; i < layers.size(); i++) {
		const PackedByteArray layer = layers[i];
		ERR_FAIL_COND_V_MSG(layer.size() < LAYER_HEADER_SIZE || layer.size() > 0xFFFF, PackedByteArray(),
				vformat("layer %d must be 4 to 65535 bytes, its layer header included", i));
	}
	byte_io::Writer out;
	write_header(out);
	write_id(out, emitting_entity_id);
	write_id(out, event_id);
	write_vector(out, relative_antenna_location);
	out.u16(system_type);
	out.u16(system_name);
	out.u8(system_mode);
	out.u8(change_options);
	out.u8(system_designator);
	out.u8(system_specific_data);
	out.u8(system_status);
	out.u8(data_field_1);
	out.u8(information_layers);
	out.u8(data_field_2);
	for (int i = 0; i < 6; i++) {
		out.u16(parameters[i]);
	}
	for (int64_t i = 0; i < layers.size(); i++) {
		out.raw(layers[i]);
	}
	return finish(out);
}

Ref<DisIffPdu> DisIffPdu::decode(const PackedByteArray &bytes) {
	if (!DisPdu::validate(bytes, DisPdu::PDU_TYPE_IFF, BASE_SIZE)) {
		return Ref<DisIffPdu>();
	}
	const int64_t length = DisPdu::get_effective_length(bytes);
	byte_io::Reader in(bytes, 0, length);
	Ref<DisIffPdu> out;
	out.instantiate();
	out->read_header(in);
	out->emitting_entity_id = read_id(in);
	out->event_id = read_id(in);
	out->relative_antenna_location = read_vector(in);
	out->system_type = static_cast<int>(in.u16());
	out->system_name = static_cast<int>(in.u16());
	out->system_mode = static_cast<int>(in.u8());
	out->change_options = static_cast<int>(in.u8());
	out->system_designator = static_cast<int>(in.u8());
	out->system_specific_data = static_cast<int>(in.u8());
	out->system_status = static_cast<int>(in.u8());
	out->data_field_1 = static_cast<int>(in.u8());
	out->information_layers = static_cast<int>(in.u8());
	out->data_field_2 = static_cast<int>(in.u8());
	for (int i = 0; i < 6; i++) {
		out->parameters.set(i, static_cast<int>(in.u16()));
	}
	// Further layers: layer number (1), layer specific information (1), length in bytes (2, header included).
	while (in.ok() && in.remaining() >= LAYER_HEADER_SIZE) {
		const int64_t start = in.position();
		in.skip(2);
		const int64_t layer_length = static_cast<int64_t>(in.u16());
		if (layer_length < LAYER_HEADER_SIZE || start + layer_length > length) {
			DisPdu::set_last_error(vformat("IFF PDU: bad layer length %d at byte %d", layer_length, start));
			return Ref<DisIffPdu>();
		}
		in.skip(layer_length - LAYER_HEADER_SIZE);
		out->layers.push_back(bytes.slice(start, start + layer_length));
	}
	if (!in.ok()) {
		DisPdu::set_last_error("IFF PDU runs past its length");
		return Ref<DisIffPdu>();
	}
	if (in.remaining() > 0) {
		DisPdu::set_last_error(vformat("IFF PDU: %d bytes after the last layer", in.remaining()));
		return Ref<DisIffPdu>();
	}
	out->keep_trailing(bytes);
	return out;
}
