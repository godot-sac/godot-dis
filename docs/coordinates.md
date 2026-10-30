# Coordinates

DIS puts every entity in one global frame: **ECEF** (Earth-Centred, Earth-Fixed, WGS84). A Godot scene wants
small numbers around (0, 0, 0). `DisGeoOrigin` connects the two.

```gdscript
@onready var origin: DisGeoOrigin = $DisGeoOrigin   # latitude 39.9334, longitude 32.8597, height 938

func _on_entity_state(pdu: DisEntityStatePdu) -> void:
	$Tank.transform = origin.dis_to_transform(pdu.location_ecef, pdu.orientation)

func publish(node: Node3D, pdu: DisEntityStatePdu) -> void:
	pdu.location_ecef = origin.local_to_ecef(node.global_position)
	pdu.orientation = origin.basis_to_dis_euler(node.global_basis)
```

## Frames and units
| Frame | Axes | Used for |
|---|---|---|
| Geodetic | latitude, longitude (degrees), height above the WGS84 ellipsoid (m) | people, maps |
| ECEF | origin at Earth's centre; +X through 0°N 0°E, +Y through 0°N 90°E, +Z through the North Pole | DIS location |
| ENU | tangent plane at the origin: +X east, +Y north, +Z up | intermediate |
| Godot | +X east, +Y up, **−Z north**, 1 unit = 1 m | the scene |
| Body | +X forward, +Y right, +Z down | DIS orientation |

- Latitude / longitude are **degrees** (what everyone writes); every other angle is **radians** (DIS and Godot).
- ECEF positions are `PackedFloat64Array [x, y, z]`. A 32-bit `Vector3` has a 0.5 m step at Earth's radius.
  The subtraction of the origin happens in doubles in C++; only the local result becomes a `Vector3`.
- Height is above the ellipsoid, not above sea level (the geoid differs by up to ~100 m; about +30 to +40 m in Türkiye).

## Static helpers (`DisGeo`)
| Method | Returns |
|---|---|
| `geodetic_to_ecef(lat, lon, height)` | `PackedFloat64Array [x, y, z]` |
| `ecef_to_geodetic(ecef)` | `PackedFloat64Array [lat, lon, height]` |
| `heading_pitch_roll_to_dis_euler(Vector3(heading, pitch, roll), lat, lon)` | `Vector3(psi, theta, phi)` |
| `dis_euler_to_heading_pitch_roll(Vector3(psi, theta, phi), lat, lon)` | `Vector3(heading, pitch, roll)` |

Heading is clockwise from true north (90° = east), pitch is nose up, roll is right wing down.

## `DisGeoOrigin` node
Properties `latitude`, `longitude`, `height`. Joins the group `dis_geo_origin`
(`get_tree().get_first_node_in_group("dis_geo_origin")`).

| Method | |
|---|---|
| `ecef_to_local(ecef)` / `local_to_ecef(position)` | positions |
| `geodetic_to_local(lat, lon, height)` / `local_to_geodetic(position)` | positions |
| `ecef_vector_to_local(v)` / `local_vector_to_ecef(v)` | directions: velocity, acceleration (no translation) |
| `dis_euler_to_basis(orientation)` / `basis_to_dis_euler(basis)` | orientation; scale is ignored |
| `dis_to_transform(location_ecef, orientation)` | both at once |
| `get_origin_ecef()`, `set_origin_geodetic(lat, lon, height)` | |

The Godot entity looks along **−Z** with **+Y** up (Godot's convention, same as `look_at`), so a level entity facing
north at the origin has the identity basis.

**Scene size.** The scene is the flat tangent plane at the origin. The Earth curves away below it: an entity on the
ground 10 km away is ~8 m below the plane, at 100 km ~785 m. This is the true 3D position, not an error, but the
ground will look curved if you place terrain on the plane. Keep scenes within a few hundred km of the origin, also
because `Vector3` precision drops to ~1.5 cm at 200 km. For a larger area, move the origin.

## Math
**Geodetic → ECEF.** With `N = a / sqrt(1 − e² sin²φ)` (radius of curvature in the prime vertical):
`x = (N + h) cos φ cos λ`, `y = (N + h) cos φ sin λ`, `z = (N(1 − e²) + h) sin φ`.

**ECEF → geodetic.** Longitude is `atan2(y, x)`. Latitude by fixed-point iteration
`φ ← atan2(z + e² N(φ) sin φ, p)` with `p = sqrt(x² + y²)`, which follows directly from the equations above; the error
shrinks ~150× per step. Height as `h = p cos φ + z sin φ − a sqrt(1 − e² sin²φ)`, which stays exact at the poles
(unlike `p / cos φ − N`).

**ENU.** At origin (φ₀, λ₀) the east, north and up unit vectors in ECEF are
`e = (−sin λ₀, cos λ₀, 0)`, `n = (−sin φ₀ cos λ₀, −sin φ₀ sin λ₀, cos φ₀)`, `u = (cos φ₀ cos λ₀, cos φ₀ sin λ₀, sin φ₀)`.
`enu = [e n u]ᵀ (ecef − ecef₀)`.

**DIS orientation.** psi, theta, phi rotate the ECEF axes into the body axes: first psi about Z, then theta about the
new Y, then phi about the new X. The matrix `R = Rz(psi) Ry(theta) Rx(phi)` has the body axes (forward, right, down)
as its columns, in ECEF. Heading / pitch / roll are the same three rotations starting from the local
north-east-down axes instead, so `R_ecef = [n e −u] · R_ned`. Converting to a Godot basis maps each body axis into
ENU and then Godot axes: `basis.x = right`, `basis.y = −down`, `basis.z = −forward`.
At theta = ±90° (gimbal lock) only psi − phi is defined; phi is returned as 0.

## How it is tested
- `tests/cpp/test_geo.cpp` (doctest, `scons tests`): 77 positions (equator, poles, antimeridian, Ankara, Everest,
  Dead Sea, orbit, random) against **pymap3d**: < 1 µm; ECEF → geodetic < 1e-12 rad and < 1 µm. ENU around three
  origins against pymap3d: < 10 µm. 66 orientations against **Open-DIS Python** (`GPS.llarpy2ecef`): rotation
  matrices equal within 1e-9. Grid round trip < 1 µm.
- `tests/unit/test_geo.gd` (GUT): the same golden data through the extension, Godot axes, and `dis_euler_to_basis`
  against Godot's own `Basis.from_euler(Vector3(pitch, −heading, −roll))` (YXZ) as an independent check.
- Golden data: `tests/golden/geo_cases.txt`, regenerate with `tools/.venv/Scripts/python.exe tools/geo_golden.py`.
