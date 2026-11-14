# Entities and dead reckoning

A minimal scene that both publishes an entity and shows everyone else's:

```
Main (Node3D)
├── DisNetwork          port 3000, site_id / application_id unique per simulation
├── DisGeoOrigin        latitude / longitude / height of the scene's (0, 0, 0)
├── DisEntityManager    remote entities appear as its children
└── Tank (Node3D, your own)
    └── DisEntity       publishes its global transform (follows Tank)
```

`DisEntity` and `DisEntityManager` find the network and the origin through the groups `dis_network` and
`dis_geo_origin`; set `network_path` / `origin_path` when there is more than one.

## Publishing: `DisEntity`
| Property | Default | |
|---|---|---|
| `entity_id` | (0, 0, 0) | 0 fields are filled in: site / application from the network, entity number assigned |
| `force_id`, `entity_type`, `marking`, `appearance`, `capabilities` | | changing one sends a PDU at once |
| `dr_algorithm` | 2 (FPW) | 4 (RVW) for aircraft and anything that turns or accelerates |
| `heartbeat_interval` | 5 s | send at least this often |
| `position_threshold`, `orientation_threshold` | 1 m, 3° | send when receivers' prediction is off by more |
| `velocity_source` | Estimated | Manual: set `linear_velocity`, `linear_acceleration`, `angular_velocity` yourself (e.g. from a `CharacterBody3D`) |
| `publishing`, `auto_update` | true | |

How it decides to send: it keeps a copy of the last PDU it sent and runs the same dead reckoning the receivers run.
Every physics frame it compares that prediction with the real transform; a PDU goes out when the error passes a
threshold, a property changed, or the heartbeat is due. A constant-speed vehicle sends one PDU per heartbeat; a
vehicle in a steady turn with DRM 4 only a few per 10 s (tested: under 15 instead of 600).

Velocity, acceleration and angular velocity are estimated from the motion between physics frames and low-pass
filtered (0.1 s / 0.25 s). Units: m/s and m/s² in global Godot axes, rad/s about the node's own axes.

When the node leaves the tree it sends one last PDU with the "deactivated" appearance bit (bit 23,
`DisEntity.APPEARANCE_DEACTIVATED`) so receivers remove it at once. Signal: `published(pdu)`.
Methods: `send_now()`, `create_pdu()`, `get_effective_entity_id()`, `process_entity(now)` (with `auto_update` off).

## Receiving: `DisEntityManager`
| Property | Default | |
|---|---|---|
| `type_map` | | `DisEntityTypeMap`: entity type pattern → `PackedScene` (below) |
| `timeout` | 12 s | remove entities not heard from (2.4 heartbeats) |
| `dead_reckoning` | true | false: entities jump to each reported position |
| `smoothing_time` | 0.5 s | corrections fade in over this time instead of jumping |
| `smoothing_max_distance` | 100 m | larger corrections (teleports) jump |
| `max_entities` | 10000 | at most this many remote entities; 0 = no limit (below) |

Smoothing starts from where the entity is shown at the moment the update arrives (the old report dead-reckoned to
that time), so a fast entity moves evenly through updates. Several jumps larger than `smoothing_max_distance` in a
row raise a warning: usually two simulations publish the same entity ID and the entity flips between them.

Anyone who can send to the port creates nodes here, so their number is capped by `max_entities`: Entity State PDUs
of further new entities are dropped (counted by `get_dropped_count()`, one warning, and `entity_limit_reached(id)`
each time the limit is reached) while known entities keep updating. Entity State PDUs with NaN or infinite values
are ignored. Raise the limit for large exercises: dead reckoning 1000 entities takes about 0.2 ms per frame
([tests and performance](#tests-and-performance)), plus whatever their scenes cost to draw.

Signals: `entity_appeared(entity)`, `entity_updated(entity)` (every PDU), `entity_removed(entity, reason)` with
reason `"timeout"`, `"deactivated"` or `"removed"`, `entity_limit_reached(entity_id)`. Methods: `get_entity(Vector3i)`,
`get_entities()`, `get_entity_count()`, `get_dropped_count()`, `remove_entity(id)`, `clear()`, `handle_entity_state(pdu)` to feed PDUs from elsewhere and
`process_entities(now)` (with `auto_update` off).

Each remote entity is a `DisRemoteEntity` (Node3D) named `Entity_site_app_number`, with the scene from the type map as
its child. It has `entity_id`, `marking`, `force_id`, `entity_type`, `get_appearance()`, `get_last_pdu()`,
`get_location_ecef()` and `get_geodetic()` (dead-reckoned).

```gdscript
func _ready() -> void:
	$DisEntityManager.entity_appeared.connect(func(e: DisRemoteEntity): print(e.marking, " joined at ", e.get_geodetic()))
```

## Ground clamping
Another simulation's terrain is never exactly ours, so its tanks float or sink on our ground. Give the manager a
ground node and land entities are put on the local terrain instead:

| Property | Default | |
|---|---|---|
| `ground_path` | | any node with `height_at(x: float, z: float) -> float` (global metres, `NAN` = no ground here), e.g. godot-geo `GeoTerrain`; empty = off |
| `ground_clamp` | `GROUND_CLAMP_LAND` | which entities: land platforms, life forms and cultural features (kinds 1, 3, 5 with domain 1); `ALL`; `DISABLED` |
| `ground_align` | true | tilt clamped entities to the slope, keeping their heading |
| `ground_align_distance` | 4 m | half the footprint over which the slope is measured |

Where `height_at` returns `NAN` (off the terrain) the reported height is kept. `is_ground_clamped(entity)` tells
whether the clamp applies to an entity. godot-dis does not depend on godot-geo: the call goes through `Object.call`.

For your own units, place the node on the ground yourself (e.g. `position.y = terrain.height_at(x, z)`); `DisEntity`
publishes wherever the node is, so other simulations see the unit on our terrain.

## Articulated and attached parts
Moving parts (a turret, a gun, a rotor, landing gear) and stores on stations travel as 16-byte variable parameter
records after the Entity State PDU's fixed part. `DisVariableParameter` builds and reads them; `DisEntity` sends
`variable_parameters` with every PDU and sends at once when they change; a remote entity's records are in
`remote.get_last_pdu().variable_parameters`.

```gdscript
entity.variable_parameters = [
	DisVariableParameter.articulated_part(4096, DisVariableParameter.METRIC_AZIMUTH, turret_yaw, 0, change),
	DisVariableParameter.articulated_part(4416, DisVariableParameter.METRIC_ELEVATION, gun_pitch, 1, change),
	DisVariableParameter.attached_part(512, PackedInt32Array([2, 2, 225, 1, 2, 3, 0])),  # a store on station 512
]
for part in DisVariableParameter.decode_all(remote.get_last_pdu().variable_parameters):
	print(DisEnums.get_enum_name("articulated_part_class", part.get("type_class", 0)), part)
```

| Bytes | Articulated part (record type 0) | Attached part (record type 1) |
|---|---|---|
| 0 | record type | record type |
| 1 | change indicator (count up on a change) | detached indicator |
| 2–3 | part attached to (0 = the entity) | part attached to |
| 4–7 | parameter type = type class (multiple of 32) + metric (0–31) | station (SISO "Attached Parts") |
| 8–11 | value, 32-bit float (radians, metres, or per second) | part's entity type (8 bytes) |
| 12–15 | padding | |

Type classes, metrics and stations are SISO-REF-010 tables (`articulated_part_class`, 287 entries;
`articulated_part_metric`; `attached_part`). Azimuth is clockwise seen from above (about the body's down axis),
elevation positive up. Records are checked against ones packed from the standard with Python `struct`
(`tools/vp_golden.py`) and against Wireshark's DIS dissector (`test_wireshark.gd`). Open-DIS Python 1.1.0 writes an
articulated part's value as a 64-bit integer and is no reference here.

## Type map
`DisEntityTypeMap` is a resource with a list of `DisEntityTypeMapping` (pattern + scene) and a `default_scene`.
Patterns are the 7 entity type fields `kind:domain:country:category:subcategory:specific:extra`; `*` matches
anything and missing trailing fields count as `*`. The most specific match wins (ties: the first in the list).

| Pattern | Matches |
|---|---|
| `1:1` | every land platform |
| `1:2:*:1` | air platforms of category 1, any country |
| `1:1:225:1:1` | one platform family of country 225 |

## Dead reckoning
Between PDUs every entity is moved with the algorithm it reported. DRM 1–9 are implemented
(`src/entity/dead_reckoning.cpp`):

| DRM | Name | Position after t | Orientation |
|---|---|---|---|
| 1 | Static | P₀ | fixed |
| 2 | FPW | P₀ + V₀t | fixed |
| 3 | RPW | P₀ + V₀t | rotates |
| 4 | RVW | P₀ + V₀t + ½A₀t² | rotates |
| 5 | FVW | P₀ + V₀t + ½A₀t² | fixed |
| 6 | FPB | P₀ + R₀ R₁ v_b | fixed |
| 7 | RPB | P₀ + R₀ R₁ v_b | rotates |
| 8 | RVB | P₀ + R₀ (R₁ v_b + R₂ a_b) | rotates |
| 9 | FVB | P₀ + R₀ (R₁ v_b + R₂ a_b) | fixed |

Position and velocity are in ECEF, and so is the acceleration of DRM 4 and 5. "Rotates" means turning at the
reported body-axis angular velocity ω: R(t) = R₀ · exp([ω]× t), computed with Rodrigues' formula (exact for a
constant ω). 0 and unknown values are static.

**Body-axis algorithms 6–9** (IEEE 1278.1 Annex E) move the velocity with the entity: the body-axis velocity
v_b = R₀ᵀ V₀ (and for 8 and 9 the dead reckoning acceleration a_b, which the PDU carries in body axes as the rate of
change of v_b) is integrated in a frame turning at ω. With K = [ω]×, x = |ω|t:

- R₁ = ∫₀ᵗ exp(Kτ) dτ = t·I + (1 − cos x)/|ω|² K + (x − sin x)/|ω|³ K²
- R₂ = ∫₀ᵗ τ·exp(Kτ) dτ = ½t²·I + (sin x − x cos x)/|ω|³ K + (½x² − x sin x − cos x + 1)/|ω|⁴ K²

(Taylor series for x < 10⁻³, where the closed forms cancel.) The path curves with ω for all four, also for 6 and 9
whose reported orientation stays fixed. A vehicle in a steady turn is therefore predicted exactly by DRM 7 (a
circle) and by DRM 8 with a_b = 0, while DRM 3 and 4 fly straight or along a parabola. `DisEntity` sends
a_b = R₀ᵀA − ω × v_b for 8 and 9 (the centripetal part is already in ω). Open-DIS Python's body-axis functions do not
turn the velocity (they use R₀ only), so 6–9 are checked against numerical integration instead (below).

Time is measured from when the PDU arrived (local clock), not from the PDU timestamp, so clocks don't need to be
synchronised. Network delay shows up as a small lag, corrected by the next update.

## Tests and performance
- `tests/cpp/test_dead_reckoning.cpp`: 100 cases against Open-DIS Python (`opendis.DeadReckoning`), positions within
  1 µm. Open-DIS Python integrates orientation through Euler angle rates to first order, so its orientations agree only
  for small steps (checked within 1e-4); the exact rotation is checked against a Runge–Kutta integration (1e-9).
  Data: `tests/golden/dr_cases.txt` from `tools/dr_golden.py`. DRM 6–9: 120 random cases against a numerical
  integration of R(t) v_b(t) (Simpson, 2000 steps; within 10 µm over 5 s), a steady turn that closes its circle, and
  the no-rotation limit equal to DRM 2 / 5.
- `tests/unit/test_entities.gd`: thresholds, heartbeat, motion estimation, smoothing, timeout / deactivation, type map,
  a publisher → UDP → manager round trip, and a 30 s steady turn: DRM 7 and 8 send about one PDU per heartbeat,
  DRM 3 several times more.
- `tools/perf_entities.gd` (debug build, Ryzen 9 7950X): 1000 entities × 5 Hz = 5000 PDU/s over loopback with no loss;
  receive + decode + dead reckoning + node transforms ≈ 0.7 ms per frame. `process_entities` alone ≈ 0.2 ms per frame
  for 1000 entities; one update ≈ 1 µs.
  ```powershell
  & $godot --headless --path . -s res://tools/perf_entities.gd -- 1000 5 5
  ```
