// C++ unit tests for src/geo/geo_math (no Godot needed). Build and run: scons tests (see docs/building.md).
// Reference values: tests/golden/geo_cases.txt, written by tools/geo_golden.py (pymap3d, Open-DIS Python).
#include <doctest.h>

#include <cmath>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "geo/geo_math.h"

using namespace dis::geo;

namespace {

constexpr double PI = 3.14159265358979323846;
double rad(double degrees) { return degrees * PI / 180.0; }

// Smallest difference between two angles, so that -180° and 180° compare equal.
double angle_diff(double a, double b) {
	return std::abs(std::remainder(a - b, 2.0 * PI));
}

double max_diff(const Mat3 &a, const Mat3 &b) {
	double worst = 0.0;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			worst = std::max(worst, std::abs(a.m[i][j] - b.m[i][j]));
		}
	}
	return worst;
}

// Each golden line: a kind ("geo", "enu", "euler") followed by numbers.
struct GoldenCase {
	std::string kind;
	std::vector<double> v;
};

std::vector<GoldenCase> load_golden(const std::string &kind) {
	std::ifstream file(GOLDEN_DIR "/geo_cases.txt");
	REQUIRE_MESSAGE(file.is_open(), "missing " GOLDEN_DIR "/geo_cases.txt; run tools/geo_golden.py");
	std::vector<GoldenCase> cases;
	std::string line;
	while (std::getline(file, line)) {
		if (line.empty() || line[0] == '#') {
			continue;
		}
		std::istringstream in(line);
		GoldenCase c;
		in >> c.kind;
		if (c.kind != kind) {
			continue;
		}
		for (double x; in >> x;) {
			c.v.push_back(x);
		}
		cases.push_back(c);
	}
	REQUIRE(!cases.empty());
	return cases;
}

} // namespace

TEST_CASE("geodetic to ECEF matches pymap3d") {
	for (const GoldenCase &c : load_golden("geo")) {
		const Vec3 ecef = geodetic_to_ecef({ rad(c.v[0]), rad(c.v[1]), c.v[2] });
		const Vec3 expected{ c.v[3], c.v[4], c.v[5] };
		CAPTURE(c.v[0]);
		CAPTURE(c.v[1]);
		CAPTURE(c.v[2]);
		CHECK((ecef - expected).length() < 1e-6);
	}
}

TEST_CASE("ECEF to geodetic matches pymap3d") {
	for (const GoldenCase &c : load_golden("geo")) {
		const Geodetic g = ecef_to_geodetic({ c.v[3], c.v[4], c.v[5] });
		CAPTURE(c.v[0]);
		CAPTURE(c.v[1]);
		CAPTURE(c.v[2]);
		CHECK(angle_diff(g.latitude, rad(c.v[0])) < 1e-12); // 1e-12 rad = 6 micrometres on the surface
		if (std::abs(c.v[0]) < 90.0) { // longitude is undefined at the poles
			CHECK(angle_diff(g.longitude, rad(c.v[1])) < 1e-12);
		}
		CHECK(std::abs(g.height - c.v[2]) < 1e-6);
	}
}

TEST_CASE("geodetic round trip over a grid") {
	double worst = 0.0;
	for (double lat = -90.0; lat <= 90.0; lat += 2.5) {
		for (double lon = -180.0; lon < 180.0; lon += 7.5) {
			for (double h : { -10000.0, 0.0, 1000.0, 50000.0, 1.0e6 }) {
				const Geodetic in{ rad(lat), rad(lon), h };
				const Vec3 ecef = geodetic_to_ecef(in);
				const Vec3 back = geodetic_to_ecef(ecef_to_geodetic(ecef));
				worst = std::max(worst, (back - ecef).length());
			}
		}
	}
	CHECK(worst < 1e-6); // micrometre, far below the 1 mm requirement
}

TEST_CASE("Earth's centre does not produce NaN") {
	const Geodetic g = ecef_to_geodetic({ 0, 0, 0 });
	CHECK(g.latitude == 0.0);
	CHECK(g.height == -WGS84_A);
	const Geodetic pole = ecef_to_geodetic({ 0, 0, WGS84_B + 10.0 });
	CHECK(pole.latitude == doctest::Approx(PI / 2));
	CHECK(pole.height == doctest::Approx(10.0).epsilon(1e-9));
}

TEST_CASE("ECEF to ENU matches pymap3d") {
	for (const GoldenCase &c : load_golden("enu")) {
		const LocalFrame frame({ rad(c.v[0]), rad(c.v[1]), c.v[2] });
		const Vec3 ecef = geodetic_to_ecef({ rad(c.v[3]), rad(c.v[4]), c.v[5] });
		const Vec3 enu = frame.ecef_to_enu(ecef);
		const Vec3 expected{ c.v[6], c.v[7], c.v[8] };
		CAPTURE(c.v[3]);
		CAPTURE(c.v[4]);
		CHECK((enu - expected).length() < 1e-5);
		CHECK((frame.enu_to_ecef(enu) - ecef).length() < 1e-6);
	}
}

TEST_CASE("ENU axes are orthonormal and right-handed") {
	const Mat3 r = enu_to_ecef_rotation(rad(39.9334), rad(32.8597));
	CHECK(max_diff(r.transposed() * r, Mat3{}) < 1e-15);
	CHECK((r.column(0).cross(r.column(1)) - r.column(2)).length() < 1e-15); // east x north = up
}

TEST_CASE("ENU <-> Godot axes") {
	const Vec3 g = enu_to_godot({ 1, 2, 3 }); // 1 m east, 2 m north, 3 m up
	CHECK(g.x == 1);
	CHECK(g.y == 3);
	CHECK(g.z == -2); // north is -Z in Godot
	const Vec3 back = godot_to_enu(g);
	CHECK(back.x == 1);
	CHECK(back.y == 2);
	CHECK(back.z == 3);
}

TEST_CASE("Euler matrix round trip") {
	std::mt19937 rng(1278);
	std::uniform_real_distribution<double> any(-PI, PI), half(-PI / 2 + 1e-3, PI / 2 - 1e-3);
	for (int i = 0; i < 1000; i++) {
		const Euler e{ any(rng), half(rng), any(rng) };
		const Euler back = matrix_to_euler(euler_to_matrix(e));
		CHECK(angle_diff(back.z, e.z) < 1e-9);
		CHECK(std::abs(back.y - e.y) < 1e-9);
		CHECK(angle_diff(back.x, e.x) < 1e-9);
	}
}

TEST_CASE("Euler gimbal lock keeps the same rotation") {
	const Euler e{ rad(30), PI / 2, rad(20) };
	const Euler back = matrix_to_euler(euler_to_matrix(e));
	CHECK(back.x == 0.0);
	CHECK(max_diff(euler_to_matrix(back), euler_to_matrix(e)) < 1e-12);
}

TEST_CASE("DIS Euler: heading east on the equator") {
	// Body +X (forward) = ECEF +Y, body +Z (down) = ECEF -X  =>  psi = 90°, theta = 0, phi = -90°.
	const Euler dis = heading_pitch_roll_to_dis_euler({ rad(90), 0, 0 }, 0, 0);
	CHECK(dis.z == doctest::Approx(rad(90)));
	CHECK(std::abs(dis.y) < 1e-12);
	CHECK(dis.x == doctest::Approx(rad(-90)));
}

TEST_CASE("DIS Euler matches Open-DIS Python") {
	for (const GoldenCase &c : load_golden("euler")) {
		const double lat = rad(c.v[0]), lon = rad(c.v[1]);
		const Euler hpr{ c.v[2], c.v[3], c.v[4] };
		const Euler expected{ c.v[5], c.v[6], c.v[7] };
		const Euler dis = heading_pitch_roll_to_dis_euler(hpr, lat, lon);
		CAPTURE(c.v[0]);
		CAPTURE(c.v[1]);
		// Compare rotations, not angle triples: two triples can describe the same orientation.
		CHECK(max_diff(euler_to_matrix(dis), euler_to_matrix(expected)) < 1e-9);
		// And back to heading / pitch / roll.
		const Euler back = dis_euler_to_heading_pitch_roll(expected, lat, lon);
		CHECK(max_diff(euler_to_matrix(back), euler_to_matrix(hpr)) < 1e-9);
	}
}
