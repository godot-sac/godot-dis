#pragma once

// Coordinate math for DIS, in plain C++ (no Godot types) so it can be unit-tested on its own (tests/cpp).
// Everything is double precision. Angles are radians, distances metres.
//
// Frames:
//   geodetic  WGS84 latitude, longitude, height above the ellipsoid
//   ECEF      DIS world coordinates: origin at Earth's centre, +X through (0°N, 0°E), +Z through the North Pole
//   ENU       local tangent plane at an origin: +X east, +Y north, +Z up
//   NED       local tangent plane: +X north, +Y east, +Z down (used for heading / pitch / roll)
//   Godot     ENU with Godot's axes: +X east, +Y up, -Z north
//   body      DIS entity axes: +X forward, +Y right, +Z down
// Notes and derivations: docs/coordinates.md.

namespace dis::geo {

// WGS84 ellipsoid.
constexpr double WGS84_A = 6378137.0; // semi-major axis (m)
constexpr double WGS84_F = 1.0 / 298.257223563; // flattening
constexpr double WGS84_E2 = WGS84_F * (2.0 - WGS84_F); // first eccentricity squared
constexpr double WGS84_B = WGS84_A * (1.0 - WGS84_F); // semi-minor axis (m)

struct Vec3 {
	double x = 0.0, y = 0.0, z = 0.0;

	Vec3 operator+(const Vec3 &o) const { return { x + o.x, y + o.y, z + o.z }; }
	Vec3 operator-(const Vec3 &o) const { return { x - o.x, y - o.y, z - o.z }; }
	Vec3 operator*(double s) const { return { x * s, y * s, z * s }; }
	double dot(const Vec3 &o) const { return x * o.x + y * o.y + z * o.z; }
	Vec3 cross(const Vec3 &o) const { return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x }; }
	double length() const;
};

// 3x3 rotation matrix, row-major: m[row][column]. A matrix that maps frame B into frame A has the
// B axes (expressed in A) as its columns.
struct Mat3 {
	double m[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };

	static Mat3 from_columns(const Vec3 &c0, const Vec3 &c1, const Vec3 &c2);
	Vec3 column(int i) const { return { m[0][i], m[1][i], m[2][i] }; }
	Vec3 operator*(const Vec3 &v) const;
	Mat3 operator*(const Mat3 &o) const;
	Mat3 transposed() const;
};

struct Geodetic {
	double latitude = 0.0; // radians, north positive
	double longitude = 0.0; // radians, east positive
	double height = 0.0; // metres above the WGS84 ellipsoid
};

// Tait-Bryan angles applied in z, y', x'' order. For DIS orientation the reference frame is ECEF
// (psi, theta, phi); for heading / pitch / roll it is the local NED frame.
struct Euler {
	double z = 0.0; // psi / heading (yaw)
	double y = 0.0; // theta / pitch
	double x = 0.0; // phi / roll
};

Vec3 geodetic_to_ecef(const Geodetic &g);
Geodetic ecef_to_geodetic(const Vec3 &ecef);

// Rotation whose columns are the east, north and up axes at (latitude, longitude), in ECEF.
// ecef_direction = R * enu_direction, enu_direction = R^T * ecef_direction.
Mat3 enu_to_ecef_rotation(double latitude, double longitude);
// Same for north, east, down.
Mat3 ned_to_ecef_rotation(double latitude, double longitude);

// Body-to-reference rotation R = Rz(z) * Ry(y) * Rx(x): its columns are the body axes in the reference frame.
Mat3 euler_to_matrix(const Euler &e);
// Inverse of euler_to_matrix. y is in [-pi/2, pi/2]; at gimbal lock (y = ±pi/2) x is set to 0.
Euler matrix_to_euler(const Mat3 &r);

// DIS orientation (relative to ECEF) <-> heading / pitch / roll (relative to local NED) at a location.
Euler dis_euler_to_heading_pitch_roll(const Euler &dis, double latitude, double longitude);
Euler heading_pitch_roll_to_dis_euler(const Euler &hpr, double latitude, double longitude);

// Local frame tied to a geodetic origin: converts ECEF positions and directions to ENU and back.
class LocalFrame {
	Geodetic origin_geodetic;
	Vec3 origin_ecef;
	Mat3 enu_axes;

public:
	LocalFrame() : LocalFrame(Geodetic{}) {}
	explicit LocalFrame(const Geodetic &origin);

	const Geodetic &origin() const { return origin_geodetic; }
	const Vec3 &origin_in_ecef() const { return origin_ecef; }
	// Columns are the origin's east / north / up axes in ECEF.
	const Mat3 &rotation() const { return enu_axes; }

	Vec3 ecef_to_enu(const Vec3 &ecef) const;
	Vec3 enu_to_ecef(const Vec3 &enu) const;
};

// ENU <-> Godot axes (+X east, +Y up, -Z north). The same mapping works for positions and directions.
inline Vec3 enu_to_godot(const Vec3 &enu) { return { enu.x, enu.z, -enu.y }; }
inline Vec3 godot_to_enu(const Vec3 &g) { return { g.x, -g.z, g.y }; }

} // namespace dis::geo
