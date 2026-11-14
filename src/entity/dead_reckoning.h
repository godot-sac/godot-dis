#pragma once

// Dead reckoning: predicts where a remote entity is between Entity State PDUs, from the last reported
// position, velocity, acceleration, orientation and angular velocity. Plain C++ like geo_math (tested in tests/cpp).
// Notes and equations: docs/entities.md.

#include "geo/geo_math.h"

namespace dis::dr {

// DIS dead reckoning algorithm numbers (the "DRM" field of the Entity State PDU).
// F/R = fixed / rotating orientation, P/V = constant velocity / constant acceleration, W/B = world / body axes.
enum Algorithm {
	ALGORITHM_OTHER = 0,
	ALGORITHM_STATIC = 1,
	ALGORITHM_FPW = 2,
	ALGORITHM_RPW = 3,
	ALGORITHM_RVW = 4,
	ALGORITHM_FVW = 5,
	ALGORITHM_FPB = 6,
	ALGORITHM_RPB = 7,
	ALGORITHM_RVB = 8,
	ALGORITHM_FVB = 9,
};

struct State {
	geo::Vec3 position; // ECEF, m
	geo::Vec3 velocity; // ECEF, m/s
	// As in the PDU's dead reckoning field: ECEF for the world-axis algorithms; for the body-axis algorithms 6-9 the
	// rate of change of the body-axis velocity, in body axes (it turns with the entity).
	geo::Vec3 acceleration; // m/s^2
	geo::Mat3 orientation; // body -> ECEF (columns: forward, right, down)
	geo::Vec3 angular_velocity; // body axes, rad/s
	int algorithm = ALGORITHM_FPW;
};

struct Pose {
	geo::Vec3 position;
	geo::Mat3 orientation;
};

// The world-axis algorithm with the same rotation and acceleration behaviour as `algorithm` (6-9 -> 2-5); 0 and
// unknown values are treated as static.
int effective_algorithm(int algorithm);
bool rotates(int algorithm);
// Body-axis algorithms (FPB, RPB, RVB, FVB): the PDU's dead reckoning acceleration is in body axes.
bool body_axes(int algorithm);
bool accelerates(int algorithm);

// Pose of `state` after `dt` seconds. Body-axis algorithms (IEEE 1278.1 Annex E): the body-axis velocity (and for
// 8 / 9 its rate of change) is integrated in a frame turning at the body rates, P = P0 + R0 (R1 v_b + R2 a_b), also
// when the reported orientation stays fixed (6, 9).
Pose extrapolate(const State &state, double dt);

// R1 = integral over [0, dt] of exp([w]x t) dt, R2 = integral of t exp([w]x t) dt (closed forms, series near w = 0).
geo::Mat3 body_displacement_r1(const geo::Vec3 &omega, double dt);
geo::Mat3 body_displacement_r2(const geo::Vec3 &omega, double dt);

// Body-axis dead reckoning acceleration of a motion with ECEF velocity `velocity` and acceleration `acceleration`,
// orientation `orientation` (body -> ECEF) and body rates `omega`: d(v_b)/dt = R^T a - w x v_b.
geo::Vec3 body_acceleration(const geo::Mat3 &orientation, const geo::Vec3 &velocity, const geo::Vec3 &acceleration,
		const geo::Vec3 &omega);

// Rotation produced by turning at body rates `omega` for `dt` seconds: exp([omega]x dt) (Rodrigues' formula).
// The new orientation is orientation * rotation_from_body_rates(omega, dt).
geo::Mat3 rotation_from_body_rates(const geo::Vec3 &omega, double dt);
// Inverse: the constant body rates that turn `from` into `to` in `dt` seconds.
geo::Vec3 body_rates_between(const geo::Mat3 &from, const geo::Mat3 &to, double dt);
// Angle (radians, 0..pi) of the rotation between two orientations.
double angle_between(const geo::Mat3 &a, const geo::Mat3 &b);

} // namespace dis::dr
