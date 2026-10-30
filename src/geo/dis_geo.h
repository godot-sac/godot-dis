#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "geo/geo_math.h"

// Static coordinate helpers for GDScript: DisGeo.geodetic_to_ecef(39.93, 32.86, 938.0).
// Latitude and longitude are degrees; every other angle is radians. ECEF positions are
// PackedFloat64Array [x, y, z] because Vector3 (32-bit) would lose about a metre at Earth's radius.
class DisGeo : public godot::Object {
	GDCLASS(DisGeo, godot::Object)

protected:
	static void _bind_methods();

public:
	static godot::PackedFloat64Array geodetic_to_ecef(double latitude, double longitude, double height);
	// Returns [latitude, longitude, height].
	static godot::PackedFloat64Array ecef_to_geodetic(const godot::PackedFloat64Array &ecef);
	// heading_pitch_roll = Vector3(heading, pitch, roll) relative to local north / horizon at the location;
	// DIS orientation = Vector3(psi, theta, phi) relative to ECEF, as in DisEntityStatePdu.orientation.
	static godot::Vector3 heading_pitch_roll_to_dis_euler(const godot::Vector3 &heading_pitch_roll, double latitude,
			double longitude);
	static godot::Vector3 dis_euler_to_heading_pitch_roll(const godot::Vector3 &orientation, double latitude,
			double longitude);
};

// Conversions between Godot values and dis::geo types, shared by the geo classes.
namespace dis::geo {
Vec3 vec3_from_packed(const godot::PackedFloat64Array &a);
godot::PackedFloat64Array packed_from_vec3(const Vec3 &v);
inline Vec3 vec3_from_vector3(const godot::Vector3 &v) { return { v.x, v.y, v.z }; }
inline godot::Vector3 vector3_from_vec3(const Vec3 &v) { return godot::Vector3(v.x, v.y, v.z); }
inline Euler euler_from_vector3(const godot::Vector3 &v) { return { v.x, v.y, v.z }; } // (z, y, x) order
inline godot::Vector3 vector3_from_euler(const Euler &e) { return godot::Vector3(e.z, e.y, e.x); }
double deg_to_rad(double degrees);
double rad_to_deg(double radians);
} // namespace dis::geo
