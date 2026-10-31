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

Signals: `entity_appeared(entity)`, `entity_updated(entity)` (every PDU), `entity_removed(entity, reason)` with
reason `"timeout"`, `"deactivated"` or `"removed"`. Methods: `get_entity(Vector3i)`, `get_entities()`,
`get_entity_count()`, `remove_entity(id)`, `clear()`, `handle_entity_state(pdu)` to feed PDUs from elsewhere and
`process_entities(now)` (with `auto_update` off).

Each remote entity is a `DisRemoteEntity` (Node3D) named `Entity_site_app_number`, with the scene from the type map as
its child. It has `entity_id`, `marking`, `force_id`, `entity_type`, `get_appearance()`, `get_last_pdu()`,
`get_location_ecef()` and `get_geodetic()` (dead-reckoned).

```gdscript
func _ready() -> void:
	$DisEntityManager.entity_appeared.connect(func(e: DisRemoteEntity): print(e.marking, " joined at ", e.get_geodetic()))
```

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
Between PDUs every entity is moved with the algorithm it reported. DRM 1–5 are implemented
(`src/entity/dead_reckoning.cpp`):

| DRM | Name | Position after t | Orientation |
|---|---|---|---|
| 1 | Static | P₀ | fixed |
| 2 | FPW | P₀ + V₀t | fixed |
| 3 | RPW | P₀ + V₀t | rotates |
| 4 | RVW | P₀ + V₀t + ½A₀t² | rotates |
| 5 | FVW | P₀ + V₀t + ½A₀t² | fixed |

Position, velocity and acceleration are in ECEF. "Rotates" means turning at the reported body-axis angular velocity
ω: R(t) = R₀ · exp([ω]× t), computed with Rodrigues' formula (exact for a constant ω). Body-axis algorithms 6–9 are
treated as their world-axis versions 2–5 for now; 0 and unknown values as static.

Time is measured from when the PDU arrived (local clock), not from the PDU timestamp, so clocks don't need to be
synchronised. Network delay shows up as a small lag, corrected by the next update.

## Tests and performance
- `tests/cpp/test_dead_reckoning.cpp`: 100 cases against Open-DIS Python (`opendis.DeadReckoning`), positions within
  1 µm. Open-DIS Python integrates orientation through Euler angle rates to first order, so its orientations agree only
  for small steps (checked within 1e-4); the exact rotation is checked against a Runge–Kutta integration (1e-9).
  Data: `tests/golden/dr_cases.txt` from `tools/dr_golden.py`.
- `tests/unit/test_entities.gd`: thresholds, heartbeat, motion estimation, smoothing, timeout / deactivation, type map,
  and a publisher → UDP → manager round trip.
- `tools/perf_entities.gd` (debug build, Ryzen 9 7950X): 1000 entities × 5 Hz = 5000 PDU/s over loopback with no loss;
  receive + decode + dead reckoning + node transforms ≈ 0.7 ms per frame. `process_entities` alone ≈ 0.2 ms per frame
  for 1000 entities; one update ≈ 1 µs.
  ```powershell
  & $godot --headless --path . -s res://tools/perf_entities.gd -- 1000 5 5
  ```
