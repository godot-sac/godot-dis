#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "geo/geo_math.h"

// The geographic point that sits at the scene's (0, 0, 0). Converts DIS (ECEF) positions and orientations to
// Godot space and back: +X east, +Y up, -Z north, 1 unit = 1 metre, on the tangent plane at the origin.
// Keep scenes within a few hundred km of the origin: the ground curves away below the tangent plane
// (about 785 m at 100 km) and 32-bit Vector3 loses precision far from (0, 0, 0).
class DisGeoOrigin : public godot::Node {
	GDCLASS(DisGeoOrigin, godot::Node)

	double latitude = 0.0; // degrees
	double longitude = 0.0; // degrees
	double height = 0.0; // metres above the WGS84 ellipsoid
	dis::geo::LocalFrame frame;

	void update_frame();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	static constexpr const char *GROUP = "dis_geo_origin";

	void set_latitude(double value);
	double get_latitude() const { return latitude; }
	void set_longitude(double value);
	double get_longitude() const { return longitude; }
	void set_height(double value);
	double get_height() const { return height; }

	void set_origin_geodetic(double p_latitude, double p_longitude, double p_height);
	godot::PackedFloat64Array get_origin_ecef() const;

	godot::Vector3 ecef_to_local(const godot::PackedFloat64Array &ecef) const;
	godot::PackedFloat64Array local_to_ecef(const godot::Vector3 &position) const;
	godot::Vector3 geodetic_to_local(double p_latitude, double p_longitude, double p_height) const;
	godot::PackedFloat64Array local_to_geodetic(const godot::Vector3 &position) const;

	// DIS orientation (psi, theta, phi) <-> Godot basis. The entity looks along -Z, +Y is its up, +X its right.
	godot::Basis dis_euler_to_basis(const godot::Vector3 &orientation) const;
	godot::Vector3 basis_to_dis_euler(const godot::Basis &basis) const;
	godot::Transform3D dis_to_transform(const godot::PackedFloat64Array &location_ecef,
			const godot::Vector3 &orientation) const;
	// ECEF direction (velocity, acceleration) <-> Godot direction.
	godot::Vector3 ecef_vector_to_local(const godot::Vector3 &vector) const;
	godot::Vector3 local_vector_to_ecef(const godot::Vector3 &vector) const;
};
