#include "dis_geo.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>

using namespace godot;

namespace dis::geo {

Vec3 vec3_from_packed(const PackedFloat64Array &a) {
	ERR_FAIL_COND_V_MSG(a.size() != 3, Vec3(), "expected an ECEF position: PackedFloat64Array of 3 values");
	return { a[0], a[1], a[2] };
}

PackedFloat64Array packed_from_vec3(const Vec3 &v) {
	PackedFloat64Array a;
	a.resize(3);
	a.set(0, v.x);
	a.set(1, v.y);
	a.set(2, v.z);
	return a;
}

double deg_to_rad(double degrees) {
	return degrees * (3.14159265358979323846 / 180.0);
}

double rad_to_deg(double radians) {
	return radians * (180.0 / 3.14159265358979323846);
}

} // namespace dis::geo

void DisGeo::_bind_methods() {
	ClassDB::bind_static_method("DisGeo", D_METHOD("geodetic_to_ecef", "latitude", "longitude", "height"),
			&DisGeo::geodetic_to_ecef);
	ClassDB::bind_static_method("DisGeo", D_METHOD("ecef_to_geodetic", "ecef"), &DisGeo::ecef_to_geodetic);
	ClassDB::bind_static_method("DisGeo",
			D_METHOD("heading_pitch_roll_to_dis_euler", "heading_pitch_roll", "latitude", "longitude"),
			&DisGeo::heading_pitch_roll_to_dis_euler);
	ClassDB::bind_static_method("DisGeo",
			D_METHOD("dis_euler_to_heading_pitch_roll", "orientation", "latitude", "longitude"),
			&DisGeo::dis_euler_to_heading_pitch_roll);
}

PackedFloat64Array DisGeo::geodetic_to_ecef(double latitude, double longitude, double height) {
	using namespace dis::geo;
	return packed_from_vec3(dis::geo::geodetic_to_ecef({ deg_to_rad(latitude), deg_to_rad(longitude), height }));
}

PackedFloat64Array DisGeo::ecef_to_geodetic(const PackedFloat64Array &ecef) {
	using namespace dis::geo;
	const Geodetic g = dis::geo::ecef_to_geodetic(vec3_from_packed(ecef));
	return packed_from_vec3({ rad_to_deg(g.latitude), rad_to_deg(g.longitude), g.height });
}

Vector3 DisGeo::heading_pitch_roll_to_dis_euler(const Vector3 &heading_pitch_roll, double latitude, double longitude) {
	using namespace dis::geo;
	return vector3_from_euler(dis::geo::heading_pitch_roll_to_dis_euler(euler_from_vector3(heading_pitch_roll),
			deg_to_rad(latitude), deg_to_rad(longitude)));
}

Vector3 DisGeo::dis_euler_to_heading_pitch_roll(const Vector3 &orientation, double latitude, double longitude) {
	using namespace dis::geo;
	return vector3_from_euler(dis::geo::dis_euler_to_heading_pitch_roll(euler_from_vector3(orientation),
			deg_to_rad(latitude), deg_to_rad(longitude)));
}
