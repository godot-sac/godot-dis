// C++ unit tests for src/entity/dead_reckoning. Reference values: tests/golden/dr_cases.txt, written by
// tools/dr_golden.py (Open-DIS Python). Orientation is also checked against numerical integration.
#include <doctest.h>

#include <cmath>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "entity/dead_reckoning.h"

using namespace dis;
using geo::Euler;
using geo::Mat3;
using geo::Vec3;

namespace {

constexpr double PI = 3.14159265358979323846;

double max_diff(const Mat3 &a, const Mat3 &b) {
	double worst = 0.0;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			worst = std::max(worst, std::abs(a.m[i][j] - b.m[i][j]));
		}
	}
	return worst;
}

Mat3 scaled_sum(const Mat3 &a, const Mat3 &b, double s) {
	Mat3 r;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			r.m[i][j] = a.m[i][j] + b.m[i][j] * s;
		}
	}
	return r;
}

// dR/dt = R * skew(omega), integrated with classic Runge-Kutta: independent of Rodrigues' formula.
Mat3 integrate_rk4(Mat3 r, const Vec3 &w, double duration, int steps) {
	Mat3 k;
	k.m[0][0] = 0;
	k.m[0][1] = -w.z;
	k.m[0][2] = w.y;
	k.m[1][0] = w.z;
	k.m[1][1] = 0;
	k.m[1][2] = -w.x;
	k.m[2][0] = -w.y;
	k.m[2][1] = w.x;
	k.m[2][2] = 0;
	const double h = duration / steps;
	for (int i = 0; i < steps; i++) {
		const Mat3 k1 = r * k;
		const Mat3 k2 = scaled_sum(r, k1, h / 2) * k;
		const Mat3 k3 = scaled_sum(r, k2, h / 2) * k;
		const Mat3 k4 = scaled_sum(r, k3, h) * k;
		r = scaled_sum(scaled_sum(scaled_sum(scaled_sum(r, k1, h / 6), k2, h / 3), k3, h / 3), k4, h / 6);
	}
	return r;
}

std::vector<std::vector<double>> load_dr_cases() {
	std::ifstream file(GOLDEN_DIR "/dr_cases.txt");
	REQUIRE_MESSAGE(file.is_open(), "missing " GOLDEN_DIR "/dr_cases.txt; run tools/dr_golden.py");
	std::vector<std::vector<double>> cases;
	std::string line;
	while (std::getline(file, line)) {
		if (line.rfind("dr ", 0) != 0) {
			continue;
		}
		std::istringstream in(line.substr(3));
		std::vector<double> v;
		for (double x; in >> x;) {
			v.push_back(x);
		}
		cases.push_back(v);
	}
	REQUIRE(cases.size() == 100);
	return cases;
}

} // namespace

TEST_CASE("dead reckoning matches Open-DIS Python") {
	for (const std::vector<double> &c : load_dr_cases()) {
		// algorithm dt  p(3) v(3) a(3) euler(3) w(3)  ->  p(3) euler(3)
		dr::State s;
		s.algorithm = static_cast<int>(c[0]);
		const double dt = c[1];
		s.position = { c[2], c[3], c[4] };
		s.velocity = { c[5], c[6], c[7] };
		s.acceleration = { c[8], c[9], c[10] };
		s.orientation = geo::euler_to_matrix({ c[11], c[12], c[13] });
		s.angular_velocity = { c[14], c[15], c[16] };
		const Vec3 expected_position{ c[17], c[18], c[19] };
		const Mat3 expected_orientation = geo::euler_to_matrix({ c[20], c[21], c[22] });

		const dr::Pose pose = dr::extrapolate(s, dt);
		CAPTURE(s.algorithm);
		CAPTURE(dt);
		CHECK((pose.position - expected_position).length() < 1e-6);
		// Open-DIS Python integrates Euler rates to first order: agreement within (|w| dt)^2.
		const double tolerance = dr::rotates(s.algorithm) ? 1e-4 : 1e-12;
		CHECK(max_diff(pose.orientation, expected_orientation) < tolerance);
	}
}

TEST_CASE("rotation from body rates matches numerical integration") {
	std::mt19937 rng(1278);
	std::uniform_real_distribution<double> rate(-1.0, 1.0), angle(-PI, PI), pitch(-1.4, 1.4);
	for (int i = 0; i < 50; i++) {
		const Mat3 start = geo::euler_to_matrix({ angle(rng), pitch(rng), angle(rng) });
		const Vec3 w{ rate(rng), rate(rng), rate(rng) };
		for (double t : { 0.1, 1.0, 5.0, 12.0 }) {
			const Mat3 exact = start * dr::rotation_from_body_rates(w, t);
			CHECK(max_diff(exact, integrate_rk4(start, w, t, 2000)) < 1e-9);
		}
	}
}

TEST_CASE("yaw rate turns the heading") {
	// Level entity at Ankara facing north; body +Z is down, so a positive yaw rate turns right (clockwise).
	const double lat = 39.9334 * PI / 180, lon = 32.8597 * PI / 180;
	dr::State s;
	s.algorithm = dr::ALGORITHM_RPW;
	s.orientation = geo::euler_to_matrix(geo::heading_pitch_roll_to_dis_euler({ 0, 0, 0 }, lat, lon));
	s.angular_velocity = { 0, 0, 0.1 };
	const dr::Pose pose = dr::extrapolate(s, 5.0);
	const Euler hpr = geo::dis_euler_to_heading_pitch_roll(geo::matrix_to_euler(pose.orientation), lat, lon);
	CHECK(hpr.z == doctest::Approx(0.5));
	CHECK(std::abs(hpr.y) < 1e-12);
	CHECK(std::abs(hpr.x) < 1e-12);
}

TEST_CASE("body rates between two orientations") {
	std::mt19937 rng(7);
	std::uniform_real_distribution<double> unit(-1.0, 1.0), angle(-PI, PI), pitch(-1.4, 1.4);
	for (int i = 0; i < 200; i++) {
		const Mat3 from = geo::euler_to_matrix({ angle(rng), pitch(rng), angle(rng) });
		Vec3 axis{ unit(rng), unit(rng), unit(rng) };
		axis = axis * (1.0 / axis.length());
		// Rotation angles from tiny to almost a half turn.
		const double turn = (i % 4 == 0) ? 1e-7 : (i % 4 == 1) ? 0.01 : (i % 4 == 2) ? 1.0 : 3.1;
		const double dt = 0.5;
		const Vec3 w = axis * (turn / dt);
		const Mat3 to = from * dr::rotation_from_body_rates(w, dt);
		CHECK(dr::angle_between(from, to) == doctest::Approx(turn).epsilon(1e-6));
		CHECK((dr::body_rates_between(from, to, dt) - w).length() < 1e-6 * std::max(1.0, w.length()));
	}
	// Exactly a half turn about body Z.
	const Mat3 from;
	const Mat3 to = dr::rotation_from_body_rates({ 0, 0, PI }, 1.0);
	const Vec3 w = dr::body_rates_between(from, to, 1.0);
	CHECK(std::abs(std::abs(w.z) - PI) < 1e-9);
	CHECK(max_diff(dr::rotation_from_body_rates(w, 1.0), to) < 1e-9);
}

TEST_CASE("algorithm mapping") {
	CHECK(dr::effective_algorithm(0) == dr::ALGORITHM_STATIC);
	CHECK(dr::effective_algorithm(1) == dr::ALGORITHM_STATIC);
	CHECK(dr::effective_algorithm(4) == dr::ALGORITHM_RVW);
	CHECK(dr::effective_algorithm(8) == dr::ALGORITHM_RVW); // body-axis approximated by world-axis
	CHECK(dr::effective_algorithm(42) == dr::ALGORITHM_STATIC);
	CHECK(dr::rotates(3));
	CHECK(!dr::rotates(5));
	CHECK(dr::accelerates(5));
	CHECK(!dr::accelerates(2));
}

namespace {

// Body-axis motion integrated numerically (Simpson's rule): dp/dt = R(t) v_b(t), v_b(t) = v_b + a_b t, with R(t)
// from rotation_from_body_rates (checked against RK4 above). Independent of the closed forms R1 / R2.
Vec3 integrate_body_rk4(const dr::State &s, double duration, int steps, bool accelerate) {
	const Vec3 vb = s.orientation.transposed() * s.velocity;
	const Vec3 ab = accelerate ? s.acceleration : Vec3{};
	Vec3 p = s.position;
	const double h = duration / steps;
	for (int i = 0; i < steps; i++) {
		const double t = i * h;
		const Mat3 r0 = s.orientation * dr::rotation_from_body_rates(s.angular_velocity, t);
		const Mat3 r1 = s.orientation * dr::rotation_from_body_rates(s.angular_velocity, t + h / 2);
		const Mat3 r2 = s.orientation * dr::rotation_from_body_rates(s.angular_velocity, t + h);
		const Vec3 k1 = r0 * (vb + ab * t);
		const Vec3 k2 = r1 * (vb + ab * (t + h / 2));
		const Vec3 k4 = r2 * (vb + ab * (t + h));
		p = p + (k1 + k2 * 4.0 + k4) * (h / 6.0); // Simpson on each step (dp/dt does not depend on p)
	}
	return p;
}

} // namespace

TEST_CASE("body-axis dead reckoning (DRM 6-9) matches numerical integration") {
	std::mt19937 rng(6789);
	std::uniform_real_distribution<double> unit(-1.0, 1.0), angle(-PI, PI), pitch(-1.4, 1.4);
	for (int algorithm : { 6, 7, 8, 9 }) {
		for (int i = 0; i < 10; i++) {
			dr::State s;
			s.algorithm = algorithm;
			s.position = { 4118639.58 + 5000 * unit(rng), 2654282.08 + 5000 * unit(rng), 4069473.68 + 5000 * unit(rng) };
			s.velocity = Vec3{ unit(rng), unit(rng), unit(rng) } * 250.0;
			s.acceleration = Vec3{ unit(rng), unit(rng), unit(rng) } * 15.0;
			s.orientation = geo::euler_to_matrix({ angle(rng), pitch(rng), angle(rng) });
			s.angular_velocity = Vec3{ unit(rng), unit(rng), unit(rng) } * 0.5;
			for (double dt : { 0.05, 1.0, 5.0 }) {
				CAPTURE(algorithm);
				CAPTURE(dt);
				const dr::Pose pose = dr::extrapolate(s, dt);
				const Vec3 expected = integrate_body_rk4(s, dt, 2000, dr::accelerates(algorithm));
				CHECK((pose.position - expected).length() < 1e-5);
				const Mat3 expected_orientation = dr::rotates(algorithm)
						? integrate_rk4(s.orientation, s.angular_velocity, dt, 2000)
						: s.orientation;
				CHECK(max_diff(pose.orientation, expected_orientation) < 1e-9);
			}
		}
	}
}

TEST_CASE("body-axis dead reckoning without rotation equals the world-axis algorithms") {
	dr::State s;
	s.position = { 4118639.58, 2654282.08, 4069473.68 };
	s.velocity = { 30.0, -12.0, 4.0 };
	s.orientation = geo::euler_to_matrix({ 0.7, -0.2, 0.3 });
	s.acceleration = { 1.0, 2.0, -0.5 }; // body axes
	for (const Vec3 &w : { Vec3{}, Vec3{ 1e-12, 0.0, -1e-12 } }) {
		s.angular_velocity = w;
		s.algorithm = dr::ALGORITHM_FPB;
		const Vec3 fpb = dr::extrapolate(s, 7.0).position;
		CHECK((fpb - (s.position + s.velocity * 7.0)).length() < 1e-6);
		s.algorithm = dr::ALGORITHM_FVB;
		const Vec3 fvb = dr::extrapolate(s, 7.0).position;
		const Vec3 world = s.position + s.velocity * 7.0 + (s.orientation * s.acceleration) * (0.5 * 49.0);
		CHECK((fvb - world).length() < 1e-6);
	}
}

TEST_CASE("a steady turn with DRM 7 closes its circle") {
	// 50 m/s forward, turning right at 0.1 rad/s: a circle of radius 500 m, once round in 2 pi / 0.1 s.
	dr::State s;
	s.algorithm = dr::ALGORITHM_RPB;
	s.position = { 4118639.58, 2654282.08, 4069473.68 };
	s.orientation = geo::euler_to_matrix({ 1.1, 0.05, -0.3 });
	s.velocity = s.orientation * Vec3{ 50.0, 0.0, 0.0 };
	s.angular_velocity = { 0.0, 0.0, 0.1 };
	const double period = 2.0 * PI / 0.1;
	const dr::Pose full = dr::extrapolate(s, period);
	CHECK((full.position - s.position).length() < 1e-6);
	CHECK(max_diff(full.orientation, s.orientation) < 1e-12);
	const dr::Pose half = dr::extrapolate(s, period / 2.0);
	CHECK(std::abs((half.position - s.position).length() - 1000.0) < 1e-6); // the diameter
	// The world-axis counterpart flies straight on.
	s.algorithm = dr::ALGORITHM_RPW;
	CHECK((dr::extrapolate(s, period).position - s.position).length() == doctest::Approx(50.0 * period));
}

TEST_CASE("body acceleration of a steady turn is zero") {
	const Mat3 r = geo::euler_to_matrix({ -2.0, 0.4, 0.9 });
	const Vec3 w{ 0.02, -0.05, 0.1 };
	const Vec3 vb{ 60.0, 3.0, -1.0 };
	// Constant body velocity in a turning frame: world acceleration R (w x v_b).
	const Vec3 a = dr::body_acceleration(r, r * vb, r * w.cross(vb), w);
	CHECK(a.length() < 1e-12);
	// Speeding up along the body x axis while turning: d(v_b)/dt = (2, 0, 0).
	const Vec3 a2 = dr::body_acceleration(r, r * vb, r * (w.cross(vb) + Vec3{ 2.0, 0.0, 0.0 }), w);
	CHECK((a2 - Vec3{ 2.0, 0.0, 0.0 }).length() < 1e-12);
}

TEST_CASE("R1 and R2 are continuous where the series takes over") {
	const Vec3 axis = Vec3{ 0.3, -0.5, 0.8 } * (1.0 / Vec3{ 0.3, -0.5, 0.8 }.length());
	const double dt = 2.0;
	for (auto f : { &dr::body_displacement_r1, &dr::body_displacement_r2 }) {
		const Mat3 below = f(axis * ((1e-3 - 1e-12) / dt), dt);
		const Mat3 above = f(axis * ((1e-3 + 1e-12) / dt), dt);
		CHECK(max_diff(below, above) < 1e-9);
	}
}
