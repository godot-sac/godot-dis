#include "dis_geo_origin.h"

#include <godot_cpp/core/class_db.hpp>

#include "geo/dis_geo.h"

using namespace godot;
using namespace dis::geo;

void DisGeoOrigin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_latitude", "value"), &DisGeoOrigin::set_latitude);
	ClassDB::bind_method(D_METHOD("get_latitude"), &DisGeoOrigin::get_latitude);
	ClassDB::bind_method(D_METHOD("set_longitude", "value"), &DisGeoOrigin::set_longitude);
	ClassDB::bind_method(D_METHOD("get_longitude"), &DisGeoOrigin::get_longitude);
	ClassDB::bind_method(D_METHOD("set_height", "value"), &DisGeoOrigin::set_height);
	ClassDB::bind_method(D_METHOD("get_height"), &DisGeoOrigin::get_height);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "latitude", PROPERTY_HINT_RANGE, "-90,90,0.0000001,suffix:°"),
			"set_latitude", "get_latitude");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "longitude", PROPERTY_HINT_RANGE, "-180,180,0.0000001,suffix:°"),
			"set_longitude", "get_longitude");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height", PROPERTY_HINT_RANGE,
						 "-1000,100000,0.01,or_less,or_greater,suffix:m"),
			"set_height", "get_height");

	ClassDB::bind_method(D_METHOD("set_origin_geodetic", "latitude", "longitude", "height"),
			&DisGeoOrigin::set_origin_geodetic);
	ClassDB::bind_method(D_METHOD("get_origin_ecef"), &DisGeoOrigin::get_origin_ecef);
	ClassDB::bind_method(D_METHOD("ecef_to_local", "ecef"), &DisGeoOrigin::ecef_to_local);
	ClassDB::bind_method(D_METHOD("local_to_ecef", "position"), &DisGeoOrigin::local_to_ecef);
	ClassDB::bind_method(D_METHOD("geodetic_to_local", "latitude", "longitude", "height"),
			&DisGeoOrigin::geodetic_to_local);
	ClassDB::bind_method(D_METHOD("local_to_geodetic", "position"), &DisGeoOrigin::local_to_geodetic);
	ClassDB::bind_method(D_METHOD("dis_euler_to_basis", "orientation"), &DisGeoOrigin::dis_euler_to_basis);
	ClassDB::bind_method(D_METHOD("basis_to_dis_euler", "basis"), &DisGeoOrigin::basis_to_dis_euler);
	ClassDB::bind_method(D_METHOD("dis_to_transform", "location_ecef", "orientation"), &DisGeoOrigin::dis_to_transform);
	ClassDB::bind_method(D_METHOD("ecef_vector_to_local", "vector"), &DisGeoOrigin::ecef_vector_to_local);
	ClassDB::bind_method(D_METHOD("local_vector_to_ecef", "vector"), &DisGeoOrigin::local_vector_to_ecef);
}

void DisGeoOrigin::_notification(int p_what) {
	// Lets other nodes (DisEntity, Phase 4) find the origin with get_tree().get_first_node_in_group().
	if (p_what == NOTIFICATION_ENTER_TREE) {
		add_to_group(GROUP);
	}
}

void DisGeoOrigin::update_frame() {
	frame = LocalFrame({ deg_to_rad(latitude), deg_to_rad(longitude), height });
}

void DisGeoOrigin::set_latitude(double value) {
	latitude = CLAMP(value, -90.0, 90.0);
	update_frame();
}

void DisGeoOrigin::set_longitude(double value) {
	longitude = value;
	update_frame();
}

void DisGeoOrigin::set_height(double value) {
	height = value;
	update_frame();
}

void DisGeoOrigin::set_origin_geodetic(double p_latitude, double p_longitude, double p_height) {
	latitude = CLAMP(p_latitude, -90.0, 90.0);
	longitude = p_longitude;
	height = p_height;
	update_frame();
}

PackedFloat64Array DisGeoOrigin::get_origin_ecef() const {
	return packed_from_vec3(frame.origin_in_ecef());
}

Vector3 DisGeoOrigin::ecef_to_local(const PackedFloat64Array &ecef) const {
	// The subtraction happens in doubles, so only the (small) local result is rounded to 32 bits.
	return vector3_from_vec3(enu_to_godot(frame.ecef_to_enu(vec3_from_packed(ecef))));
}

PackedFloat64Array DisGeoOrigin::local_to_ecef(const Vector3 &position) const {
	return packed_from_vec3(frame.enu_to_ecef(godot_to_enu(vec3_from_vector3(position))));
}

Vector3 DisGeoOrigin::geodetic_to_local(double p_latitude, double p_longitude, double p_height) const {
	const Vec3 ecef = dis::geo::geodetic_to_ecef({ deg_to_rad(p_latitude), deg_to_rad(p_longitude), p_height });
	return vector3_from_vec3(enu_to_godot(frame.ecef_to_enu(ecef)));
}

PackedFloat64Array DisGeoOrigin::local_to_geodetic(const Vector3 &position) const {
	const Geodetic g = dis::geo::ecef_to_geodetic(frame.enu_to_ecef(godot_to_enu(vec3_from_vector3(position))));
	return packed_from_vec3({ rad_to_deg(g.latitude), rad_to_deg(g.longitude), g.height });
}

Vector3 DisGeoOrigin::ecef_vector_to_local(const Vector3 &vector) const {
	return vector3_from_vec3(enu_to_godot(frame.rotation().transposed() * vec3_from_vector3(vector)));
}

Vector3 DisGeoOrigin::local_vector_to_ecef(const Vector3 &vector) const {
	return vector3_from_vec3(frame.rotation() * godot_to_enu(vec3_from_vector3(vector)));
}

Vector3 DisGeoOrigin::ecef_vec3_to_local(const Vec3 &ecef) const {
	return vector3_from_vec3(enu_to_godot(frame.ecef_to_enu(ecef)));
}

Vec3 DisGeoOrigin::local_to_ecef_vec3(const Vector3 &position) const {
	return frame.enu_to_ecef(godot_to_enu(vec3_from_vector3(position)));
}

Basis DisGeoOrigin::dis_euler_to_basis(const Vector3 &orientation) const {
	return body_matrix_to_basis(euler_to_matrix(euler_from_vector3(orientation)));
}

Vector3 DisGeoOrigin::basis_to_dis_euler(const Basis &basis) const {
	return vector3_from_euler(matrix_to_euler(basis_to_body_matrix(basis)));
}

Basis DisGeoOrigin::body_matrix_to_basis(const Mat3 &body) const {
	// Body axes (forward, right, down) in ECEF, then in Godot space.
	const Mat3 ecef_to_enu = frame.rotation().transposed();
	const Vec3 forward = enu_to_godot(ecef_to_enu * body.column(0));
	const Vec3 right = enu_to_godot(ecef_to_enu * body.column(1));
	const Vec3 down = enu_to_godot(ecef_to_enu * body.column(2));
	// Godot node axes: +X right, +Y up, +Z backward.
	return Basis(vector3_from_vec3(right), vector3_from_vec3(down * -1.0), vector3_from_vec3(forward * -1.0));
}

Mat3 DisGeoOrigin::basis_to_body_matrix(const Basis &basis) const {
	const Basis b = basis.orthonormalized(); // drop scale
	const Vec3 forward = frame.rotation() * godot_to_enu(vec3_from_vector3(-b.get_column(2)));
	const Vec3 right = frame.rotation() * godot_to_enu(vec3_from_vector3(b.get_column(0)));
	const Vec3 down = frame.rotation() * godot_to_enu(vec3_from_vector3(-b.get_column(1)));
	return Mat3::from_columns(forward, right, down);
}

Transform3D DisGeoOrigin::dis_to_transform(const PackedFloat64Array &location_ecef, const Vector3 &orientation) const {
	return Transform3D(dis_euler_to_basis(orientation), ecef_to_local(location_ecef));
}
