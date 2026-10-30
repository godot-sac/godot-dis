#include "geo_math.h"

#include <algorithm>
#include <cmath>

namespace dis::geo {

double Vec3::length() const {
	return std::sqrt(dot(*this));
}

Mat3 Mat3::from_columns(const Vec3 &c0, const Vec3 &c1, const Vec3 &c2) {
	Mat3 r;
	const Vec3 columns[3] = { c0, c1, c2 };
	for (int i = 0; i < 3; i++) {
		r.m[0][i] = columns[i].x;
		r.m[1][i] = columns[i].y;
		r.m[2][i] = columns[i].z;
	}
	return r;
}

Vec3 Mat3::operator*(const Vec3 &v) const {
	return {
		m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
		m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
		m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z,
	};
}

Mat3 Mat3::operator*(const Mat3 &o) const {
	Mat3 r;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j];
		}
	}
	return r;
}

Mat3 Mat3::transposed() const {
	Mat3 r;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			r.m[i][j] = m[j][i];
		}
	}
	return r;
}

Vec3 geodetic_to_ecef(const Geodetic &g) {
	const double sin_lat = std::sin(g.latitude);
	const double cos_lat = std::cos(g.latitude);
	// Prime vertical radius of curvature: distance from the surface to the polar axis along the normal.
	const double n = WGS84_A / std::sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat);
	return {
		(n + g.height) * cos_lat * std::cos(g.longitude),
		(n + g.height) * cos_lat * std::sin(g.longitude),
		(n * (1.0 - WGS84_E2) + g.height) * sin_lat,
	};
}

Geodetic ecef_to_geodetic(const Vec3 &ecef) {
	const double p = std::hypot(ecef.x, ecef.y); // distance from the polar axis
	Geodetic g;
	if (p == 0.0 && ecef.z == 0.0) {
		g.height = -WGS84_A; // Earth's centre: latitude is undefined
		return g;
	}
	g.longitude = std::atan2(ecef.y, ecef.x);

	// Fixed-point iteration on latitude. From geodetic_to_ecef:
	//   p = (N + h) cos(lat),  z + e2 N sin(lat) = (N + h) sin(lat)  =>  lat = atan2(z + e2 N sin(lat), p).
	// The error shrinks by about e2 (~0.0067) per step, so a few steps reach full double precision
	// anywhere outside the Earth's core.
	double lat = std::atan2(ecef.z, p * (1.0 - WGS84_E2));
	for (int i = 0; i < 16; i++) {
		const double sin_lat = std::sin(lat);
		const double n = WGS84_A / std::sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat);
		const double next = std::atan2(ecef.z + WGS84_E2 * n * sin_lat, p);
		const bool done = std::abs(next - lat) < 1e-15;
		lat = next;
		if (done) {
			break;
		}
	}
	g.latitude = lat;
	// h = p cos(lat) + z sin(lat) - a sqrt(1 - e2 sin^2(lat)); unlike p / cos(lat) - N this stays exact at the poles.
	const double sin_lat = std::sin(lat);
	g.height = p * std::cos(lat) + ecef.z * sin_lat - WGS84_A * std::sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat);
	return g;
}

Mat3 enu_to_ecef_rotation(double latitude, double longitude) {
	const double sl = std::sin(latitude), cl = std::cos(latitude);
	const double so = std::sin(longitude), co = std::cos(longitude);
	const Vec3 east{ -so, co, 0.0 };
	const Vec3 north{ -sl * co, -sl * so, cl };
	const Vec3 up{ cl * co, cl * so, sl };
	return Mat3::from_columns(east, north, up);
}

Mat3 ned_to_ecef_rotation(double latitude, double longitude) {
	const Mat3 enu = enu_to_ecef_rotation(latitude, longitude);
	return Mat3::from_columns(enu.column(1), enu.column(0), enu.column(2) * -1.0);
}

Mat3 euler_to_matrix(const Euler &e) {
	const double cz = std::cos(e.z), sz = std::sin(e.z);
	const double cy = std::cos(e.y), sy = std::sin(e.y);
	const double cx = std::cos(e.x), sx = std::sin(e.x);
	// Rz(z) * Ry(y) * Rx(x), multiplied out.
	Mat3 r;
	r.m[0][0] = cz * cy;
	r.m[0][1] = cz * sy * sx - sz * cx;
	r.m[0][2] = cz * sy * cx + sz * sx;
	r.m[1][0] = sz * cy;
	r.m[1][1] = sz * sy * sx + cz * cx;
	r.m[1][2] = sz * sy * cx - cz * sx;
	r.m[2][0] = -sy;
	r.m[2][1] = cy * sx;
	r.m[2][2] = cy * cx;
	return r;
}

Euler matrix_to_euler(const Mat3 &r) {
	Euler e;
	e.y = std::asin(std::clamp(-r.m[2][0], -1.0, 1.0));
	// cos(y) is ~0 at gimbal lock; then only z - x (or z + x) is defined, so x is fixed at 0.
	if (std::hypot(r.m[0][0], r.m[1][0]) < 1e-12) {
		e.x = 0.0;
		e.z = std::atan2(-r.m[0][1], r.m[1][1]);
	} else {
		e.z = std::atan2(r.m[1][0], r.m[0][0]);
		e.x = std::atan2(r.m[2][1], r.m[2][2]);
	}
	return e;
}

Euler dis_euler_to_heading_pitch_roll(const Euler &dis, double latitude, double longitude) {
	// body->NED = (NED->ECEF)^T * body->ECEF
	const Mat3 body_to_ned = ned_to_ecef_rotation(latitude, longitude).transposed() * euler_to_matrix(dis);
	return matrix_to_euler(body_to_ned);
}

Euler heading_pitch_roll_to_dis_euler(const Euler &hpr, double latitude, double longitude) {
	const Mat3 body_to_ecef = ned_to_ecef_rotation(latitude, longitude) * euler_to_matrix(hpr);
	return matrix_to_euler(body_to_ecef);
}

LocalFrame::LocalFrame(const Geodetic &origin) :
		origin_geodetic(origin),
		origin_ecef(geodetic_to_ecef(origin)),
		enu_axes(enu_to_ecef_rotation(origin.latitude, origin.longitude)) {
}

Vec3 LocalFrame::ecef_to_enu(const Vec3 &ecef) const {
	return enu_axes.transposed() * (ecef - origin_ecef);
}

Vec3 LocalFrame::enu_to_ecef(const Vec3 &enu) const {
	return origin_ecef + enu_axes * enu;
}

} // namespace dis::geo
