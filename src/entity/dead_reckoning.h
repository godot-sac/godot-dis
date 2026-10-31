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
	geo::Vec3 acceleration; // ECEF, m/s^2
	geo::Mat3 orientation; // body -> ECEF (columns: forward, right, down)
	geo::Vec3 angular_velocity; // body axes, rad/s
	int algorithm = ALGORITHM_FPW;
};

struct Pose {
	geo::Vec3 position;
	geo::Mat3 orientation;
};

// The world-axis algorithm used for `algorithm`. Body-axis algorithms 6-9 are approximated by their world-axis
// counterparts 2-5 (v0.1 scope); 0 and unknown values are treated as static.
int effective_algorithm(int algorithm);
bool rotates(int algorithm);
bool accelerates(int algorithm);

// Pose of `state` after `dt` seconds.
Pose extrapolate(const State &state, double dt);

// Rotation produced by turning at body rates `omega` for `dt` seconds: exp([omega]x dt) (Rodrigues' formula).
// The new orientation is orientation * rotation_from_body_rates(omega, dt).
geo::Mat3 rotation_from_body_rates(const geo::Vec3 &omega, double dt);
// Inverse: the constant body rates that turn `from` into `to` in `dt` seconds.
geo::Vec3 body_rates_between(const geo::Mat3 &from, const geo::Mat3 &to, double dt);
// Angle (radians, 0..pi) of the rotation between two orientations.
double angle_between(const geo::Mat3 &a, const geo::Mat3 &b);

} // namespace dis::dr
