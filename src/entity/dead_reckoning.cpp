#include "dead_reckoning.h"

#include <algorithm>
#include <cmath>

namespace dis::dr {

using geo::Mat3;
using geo::Vec3;

int effective_algorithm(int algorithm) {
	switch (algorithm) {
		case ALGORITHM_FPW:
		case ALGORITHM_RPW:
		case ALGORITHM_RVW:
		case ALGORITHM_FVW:
			return algorithm;
		case ALGORITHM_FPB:
		case ALGORITHM_RPB:
		case ALGORITHM_RVB:
		case ALGORITHM_FVB:
			return algorithm - 4;
		default:
			return ALGORITHM_STATIC;
	}
}

bool rotates(int algorithm) {
	const int a = effective_algorithm(algorithm);
	return a == ALGORITHM_RPW || a == ALGORITHM_RVW;
}

bool body_axes(int algorithm) {
	return algorithm >= ALGORITHM_FPB && algorithm <= ALGORITHM_FVB;
}

bool accelerates(int algorithm) {
	const int a = effective_algorithm(algorithm);
	return a == ALGORITHM_RVW || a == ALGORITHM_FVW;
}

Pose extrapolate(const State &state, double dt) {
	Pose pose{ state.position, state.orientation };
	const int algorithm = effective_algorithm(state.algorithm);
	if (algorithm == ALGORITHM_STATIC) {
		return pose;
	}
	if (body_axes(state.algorithm)) {
		const Vec3 v_body = state.orientation.transposed() * state.velocity;
		Vec3 d = body_displacement_r1(state.angular_velocity, dt) * v_body;
		if (accelerates(algorithm)) {
			d = d + body_displacement_r2(state.angular_velocity, dt) * state.acceleration;
		}
		pose.position = state.position + state.orientation * d;
		if (rotates(algorithm)) {
			pose.orientation = state.orientation * rotation_from_body_rates(state.angular_velocity, dt);
		}
		return pose;
	}
	pose.position = state.position + state.velocity * dt;
	if (accelerates(algorithm)) {
		pose.position = pose.position + state.acceleration * (0.5 * dt * dt);
	}
	if (rotates(algorithm)) {
		pose.orientation = state.orientation * rotation_from_body_rates(state.angular_velocity, dt);
	}
	return pose;
}

// Cross-product matrix: skew(k) * v == k.cross(v).
static Mat3 skew(const Vec3 &k) {
	Mat3 r;
	r.m[0][0] = 0.0;
	r.m[0][1] = -k.z;
	r.m[0][2] = k.y;
	r.m[1][0] = k.z;
	r.m[1][1] = 0.0;
	r.m[1][2] = -k.x;
	r.m[2][0] = -k.y;
	r.m[2][1] = k.x;
	r.m[2][2] = 0.0;
	return r;
}

static Mat3 add(const Mat3 &a, const Mat3 &b, double scale_b) {
	Mat3 r;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			r.m[i][j] = a.m[i][j] + b.m[i][j] * scale_b;
		}
	}
	return r;
}

// a I + b K + c K^2 with K = skew(k).
static Mat3 polynomial(const Vec3 &k, double a, double b, double c) {
	const Mat3 s = skew(k);
	return add(add(add(Mat3{}, Mat3{}, a - 1.0), s, b), s * s, c);
}

// With x = |w| dt, exp([w]x t) = I + sin(|w| t)/|w| K + (1 - cos(|w| t))/|w|^2 K^2 (K = skew(w)); integrating
// term by term gives the coefficients below. Near x = 0 they lose all digits to cancellation, so the first two terms
// of their Taylor series are used there (error of the next term below 1e-13 relative for x < 1e-3).
Mat3 body_displacement_r1(const Vec3 &omega, double dt) {
	const double w = omega.length();
	const double x = w * dt;
	double b, c;
	if (x < 1e-3) {
		b = dt * dt * (0.5 - x * x / 24.0);
		c = dt * dt * dt * (1.0 / 6.0 - x * x / 120.0);
	} else {
		b = (1.0 - std::cos(x)) / (w * w);
		c = (x - std::sin(x)) / (w * w * w);
	}
	return polynomial(omega, dt, b, c);
}

Mat3 body_displacement_r2(const Vec3 &omega, double dt) {
	const double w = omega.length();
	const double x = w * dt;
	double b, c;
	if (x < 1e-3) {
		b = dt * dt * dt * (1.0 / 3.0 - x * x / 30.0);
		c = dt * dt * dt * dt * (0.125 - x * x / 144.0);
	} else {
		b = (std::sin(x) - x * std::cos(x)) / (w * w * w);
		c = (0.5 * x * x - x * std::sin(x) - std::cos(x) + 1.0) / (w * w * w * w);
	}
	return polynomial(omega, 0.5 * dt * dt, b, c);
}

Vec3 body_acceleration(const Mat3 &orientation, const Vec3 &velocity, const Vec3 &acceleration, const Vec3 &omega) {
	const Mat3 to_body = orientation.transposed();
	return to_body * acceleration - omega.cross(to_body * velocity);
}

Mat3 rotation_from_body_rates(const Vec3 &omega, double dt) {
	const double rate = omega.length();
	const double angle = rate * dt;
	if (std::abs(angle) < 1e-12) {
		return Mat3{};
	}
	// Rodrigues: R = I + sin(a) K + (1 - cos(a)) K^2, K = skew(unit axis).
	const Mat3 k = skew(omega * (1.0 / rate));
	return add(add(Mat3{}, k, std::sin(angle)), k * k, 1.0 - std::cos(angle));
}

double angle_between(const Mat3 &a, const Mat3 &b) {
	const Mat3 d = a.transposed() * b;
	const double trace = d.m[0][0] + d.m[1][1] + d.m[2][2];
	// acos is inaccurate near 0; atan2 of the sine part (from the skew-symmetric part) and cosine is not.
	const Vec3 s{ d.m[2][1] - d.m[1][2], d.m[0][2] - d.m[2][0], d.m[1][0] - d.m[0][1] };
	return std::atan2(0.5 * s.length(), 0.5 * (trace - 1.0));
}

Vec3 body_rates_between(const Mat3 &from, const Mat3 &to, double dt) {
	if (dt <= 0.0) {
		return {};
	}
	const Mat3 d = from.transposed() * to; // rotation in body axes
	const Vec3 s{ d.m[2][1] - d.m[1][2], d.m[0][2] - d.m[2][0], d.m[1][0] - d.m[0][1] }; // 2 sin(a) * axis
	const double angle = angle_between(from, to);
	const double sin_angle = 0.5 * s.length();
	if (sin_angle < 1e-9) {
		if (angle < 1.0) {
			return s * (0.5 / dt); // tiny rotation: sin(a) ~ a
		}
		// Half turn: the axis is the column of (d + I) with the largest norm.
		const Mat3 p = add(d, Mat3{}, 1.0);
		Vec3 axis = p.column(0);
		for (int i = 1; i < 3; i++) {
			if (p.column(i).length() > axis.length()) {
				axis = p.column(i);
			}
		}
		return axis * (angle / (axis.length() * dt));
	}
	return s * (angle / (2.0 * sin_angle * dt));
}

} // namespace dis::dr
