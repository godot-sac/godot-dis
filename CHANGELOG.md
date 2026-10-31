# Changelog

All notable changes to this project are documented here. Versions follow [semver](https://semver.org/).

## [Unreleased]
### Added
- DIS 7 Entity State PDU (`DisEntityStatePdu`), reading DIS 6 too; byte-for-byte tests against Open-DIS Python.
- `DisNetwork`: UDP broadcast / unicast / multicast, exercise and site / application filters, port sharing.
- `DisGeoOrigin`, `DisGeo`: WGS84 / ECEF / local ENU / Godot conversions and DIS Euler angles, in doubles.
- `DisEntity` (publishing with thresholds and heartbeat) and `DisEntityManager` (remote entities, dead reckoning
  DRM 1-5, smoothing, timeouts), `DisEntityTypeMap`.
- `DisEnums`: SISO-REF-010 names and entity type to MIL-STD-2525D symbol codes.
- `DisEntityManager` ground clamping: `ground_path` (any node with `height_at(x, z)`, e.g. godot-geo `GeoTerrain`),
  `ground_clamp`, `ground_align`.
- Warning when an entity jumps far on several updates in a row (two simulations sharing one entity ID).
- Demo: two-sided exercise around Ankara plus civilian traffic from Open-DIS Python (`--port`; traffic `--start`,
  `--rate`, dead reckoning algorithm 4 with turn rate, racetrack routes); `tools/link_milsymbol.py`.
- CI for Windows, Linux and macOS.

### Fixed
- Smoothing started from the previous frame's pose, so fast entities stalled for a frame and caught up on every
  update (visible as jitter on the demo airliner).
