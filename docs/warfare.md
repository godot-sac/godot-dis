# Weapons: Fire and Detonation

A weapon engagement in DIS is two PDUs of the Warfare family (protocol family 2): a **Fire** PDU when the weapon is
fired and a **Detonation** PDU where the munition explodes or hits. Both carry the same event ID, so receivers can
pair them. godot-dis encodes and decodes both and adds helpers to `DisEntity`.

| PDU | Type | Size | Class |
|---|---:|---:|---|
| Fire | 2 | 96 | `DisFirePdu` |
| Detonation | 3 | 104 + 16 per variable parameter | `DisDetonationPdu` |

Both derive from `DisWarfarePdu`: `firing_entity_id`, `target_entity_id`, `munition_id` (Fire: the munition /
expendable ID; Detonation: the exploding entity ID), `event_id` (site, application, event number),
`location_ecef` (3 doubles), `velocity` (ECEF, m/s) and the munition descriptor `munition_type` (7 ints, kind 2),
`warhead`, `fuse`, `quantity`, `rate`. Fire adds `fire_mission_index` and `range` (m, 0 = unknown); Detonation adds
`location_in_entity` (impact point in the target's body frame), `detonation_result` and raw 16-byte
`variable_parameters`.

## Sending

```gdscript
# `tank` is a DisEntity; positions and velocities are global Godot coordinates (the DisGeoOrigin converts them).
const ROUND_120MM := PackedInt32Array([2, 2, 0, 2, 0, 0, 0])  # munition, anti-armor, ballistic
var fire := tank.fire(ROUND_120MM, target_id, direction * 1500.0, distance)   # sent now, returned
# ... time of flight later:
tank.detonate(impact_point, DisDetonationPdu.RESULT_ENTITY_IMPACT, fire)       # same event, munition and target
```

`fire()` fills in the firing entity (this one), its current position, a new event ID (unique per application),
`munition_id` = (site, application, 0) — the round is not simulated as an entity — and quantity 1.
`detonate()` copies event ID, munition descriptor and target from the Fire PDU you pass; without one it gets a new
event ID. To set more fields (warhead, fuse, fire mission, a burst of several rounds), build the PDU with
`create_fire_pdu()` / `create_detonation_pdu()`, change it, and send it with `DisNetwork.send_pdu()`.

Common `detonation_result` values are constants on `DisDetonationPdu` (`RESULT_ENTITY_IMPACT`,
`RESULT_GROUND_IMPACT`, `RESULT_AIR_BURST`, ...); `DisEnums.get_enum_name("detonation_result", value)` names all 34.
`warhead` and `fuse` use SISO-REF-010 tables 60 and 61 (`DisEnums.get_enum_name("warhead", 1000)` = "High
Explosive (HE)").

## Receiving

```gdscript
network.fire_received.connect(func(pdu: DisFirePdu):
    var from := origin.ecef_to_local(pdu.location_ecef)
    show_muzzle_flash(from))
network.detonation_received.connect(func(pdu: DisDetonationPdu):
    show_explosion(origin.ecef_to_local(pdu.location_ecef))
    if pdu.detonation_result == DisDetonationPdu.RESULT_ENTITY_IMPACT and is_mine(pdu.target_entity_id):
        apply_damage(pdu))
```

Who decides the damage is up to the exercise; in DIS the target's owner usually does and reports it in its
Entity State appearance (damage bits 3–4 for platforms: 0 none, 1 slight, 2 moderate, 3 destroyed). The demo does
exactly that, and `DisEnums.appearance_to_status(appearance)` turns those bits into the MIL-STD-2525D status digit
(3 damaged, 4 destroyed) for `entity_type_to_sidc()`.

## Byte layouts (DIS 7, big-endian)

From open-dis-cpp's DIS 7 classes, checked byte for byte against Open-DIS Python and opendis7-java (see
[interoperability.md](interoperability.md)). After the 12-byte header:

Both start with:

| Offset | Size | Field |
|---:|---:|---|
| 12 | 6 | Firing entity ID (site, application, entity: 3 × uint16) |
| 18 | 6 | Target entity ID |
| 24 | 6 | Fire: munition / expendable ID; Detonation: exploding entity ID |
| 30 | 6 | Event ID (site, application, event number) |

Fire, then:

| Offset | Size | Field |
|---:|---:|---|
| 36 | 4 | Fire mission index (uint32) |
| 40 | 24 | Location (3 × float64, ECEF) |
| 64 | 16 | Munition descriptor: type (8), warhead (2), fuse (2), quantity (2), rate (2) |
| 80 | 12 | Velocity (3 × float32) |
| 92 | 4 | Range (float32, m) |

Detonation, then:

| Offset | Size | Field |
|---:|---:|---|
| 36 | 12 | Velocity (3 × float32) |
| 48 | 24 | Location (3 × float64, ECEF) |
| 72 | 16 | Munition descriptor |
| 88 | 12 | Location in entity coordinates (3 × float32) |
| 100 | 1 | Detonation result (SISO-REF-010 UID 62) |
| 101 | 1 | Number of variable parameters N |
| 102 | 2 | Padding |
| 104 | 16 × N | Variable parameters |

DIS 6 uses the same sizes (the descriptor is called a burst descriptor there), so DIS 6 Fire / Detonation PDUs
decode too; the PDU status bits differ.
