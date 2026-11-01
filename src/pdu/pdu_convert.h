#pragma once

#include <dis7/ClockTime.h>
#include <dis7/EntityID.h>
#include <dis7/EntityType.h>
#include <dis7/EventIdentifier.h>
#include <dis7/Vector3Double.h>
#include <dis7/Vector3Float.h>

#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

// Conversions between Godot values and open-dis-cpp records, shared by all PDU classes.
// Identifiers are Vector3i (site, application, number); entity types are 7 ints
// (kind, domain, country, category, subcategory, specific, extra); world coordinates are 3 doubles.
namespace pdu_convert {

constexpr int ENTITY_TYPE_FIELDS = 7;

inline godot::PackedInt32Array zeros(int count) {
	godot::PackedInt32Array array;
	array.resize(count);
	array.fill(0);
	return array;
}

inline godot::PackedFloat64Array zero_location() {
	godot::PackedFloat64Array array;
	array.resize(3);
	array.fill(0.0);
	return array;
}

// `value` cut or padded with zeros to `count` bytes.
inline godot::PackedByteArray resized(const godot::PackedByteArray &value, int count) {
	godot::PackedByteArray out = value;
	const int64_t old_size = out.size();
	out.resize(count);
	for (int64_t i = old_size; i < count; i++) {
		out[i] = 0;
	}
	return out;
}

// Copies `count` bytes from a Godot array into a raw buffer, padding with zeros.
inline void copy_bytes(const godot::PackedByteArray &from, unsigned char *to, int count) {
	for (int i = 0; i < count; i++) {
		to[i] = i < from.size() ? from[i] : 0;
	}
}

inline DIS::EntityType to_dis_type(const godot::PackedInt32Array &type) {
	DIS::EntityType out;
	out.setEntityKind(static_cast<unsigned char>(type[0]));
	out.setDomain(static_cast<unsigned char>(type[1]));
	out.setCountry(static_cast<unsigned short>(type[2]));
	out.setCategory(static_cast<unsigned char>(type[3]));
	out.setSubcategory(static_cast<unsigned char>(type[4]));
	out.setSpecific(static_cast<unsigned char>(type[5]));
	out.setExtra(static_cast<unsigned char>(type[6]));
	return out;
}

inline godot::PackedInt32Array from_dis_type(const DIS::EntityType &type) {
	godot::PackedInt32Array out;
	out.push_back(type.getEntityKind());
	out.push_back(type.getDomain());
	out.push_back(type.getCountry());
	out.push_back(type.getCategory());
	out.push_back(type.getSubcategory());
	out.push_back(type.getSpecific());
	out.push_back(type.getExtra());
	return out;
}

inline DIS::Vector3Float to_dis_vector(const godot::Vector3 &v) {
	DIS::Vector3Float out;
	out.setX(static_cast<float>(v.x));
	out.setY(static_cast<float>(v.y));
	out.setZ(static_cast<float>(v.z));
	return out;
}

inline godot::Vector3 from_dis_vector(const DIS::Vector3Float &v) {
	return godot::Vector3(v.getX(), v.getY(), v.getZ());
}

inline DIS::Vector3Double to_dis_location(const godot::PackedFloat64Array &location) {
	DIS::Vector3Double out;
	out.setX(location[0]);
	out.setY(location[1]);
	out.setZ(location[2]);
	return out;
}

inline godot::PackedFloat64Array from_dis_location(const DIS::Vector3Double &location) {
	godot::PackedFloat64Array out;
	out.push_back(location.getX());
	out.push_back(location.getY());
	out.push_back(location.getZ());
	return out;
}

inline DIS::SimulationAddress to_dis_address(const godot::Vector3i &id) {
	DIS::SimulationAddress address;
	address.setSite(static_cast<unsigned short>(id.x));
	address.setApplication(static_cast<unsigned short>(id.y));
	return address;
}

inline DIS::EntityID to_dis_entity_id(const godot::Vector3i &id) {
	DIS::EntityID out;
	out.setSimulationAddress(to_dis_address(id));
	out.setEntityNumber(static_cast<unsigned short>(id.z));
	return out;
}

inline godot::Vector3i from_dis_entity_id(const DIS::EntityID &id) {
	// open-dis-cpp has no const getter for the address.
	DIS::EntityID copy = id;
	return godot::Vector3i(copy.getSimulationAddress().getSite(), copy.getSimulationAddress().getApplication(),
			copy.getEntityNumber());
}

inline DIS::EventIdentifier to_dis_event_id(const godot::Vector3i &id) {
	DIS::EventIdentifier out;
	out.setSimulationAddress(to_dis_address(id));
	out.setEventNumber(static_cast<unsigned short>(id.z));
	return out;
}

inline godot::Vector3i from_dis_event_id(const DIS::EventIdentifier &id) {
	DIS::EventIdentifier copy = id;
	return godot::Vector3i(copy.getSimulationAddress().getSite(), copy.getSimulationAddress().getApplication(),
			copy.getEventNumber());
}

inline DIS::ClockTime to_dis_clock(int64_t hour, int64_t time_past_hour) {
	DIS::ClockTime out;
	out.setHour(static_cast<unsigned int>(hour));
	out.setTimePastHour(static_cast<unsigned int>(time_past_hour));
	return out;
}

} // namespace pdu_convert
