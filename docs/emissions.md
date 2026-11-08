# Emissions: Electromagnetic Emission, Designator, IFF

The Distributed Emission Regeneration family (protocol family 6) tells other simulations what an entity emits: its
radars and jammers (**Electromagnetic Emission**, type 23), a laser designator spot (**Designator**, 24) and its
IFF / SSR transponder or interrogator (**IFF**, 28). godot-dis encodes, sends, receives and decodes these PDUs; it
does not model what an emission detects. A game decides that itself, for example with a line of sight query from
godot-geo.

Each type has a `DisNetwork` signal: `electromagnetic_emission_received`, `designator_received` and `iff_received`
(and `pdu_received` for all). The own-packet filter (`ignore_own_packets`) applies to this family too: its PDUs
start with the emitting entity's site and application.

## Electromagnetic Emission
One PDU describes all emitter systems of one entity. Systems and their beams are Dictionaries;
`make_system()` and `make_beam()` return them with every key at its default.

```gdscript
var radar := DisElectromagneticEmissionPdu.make_system()
radar.emitter_name = 1000         # SISO-REF-010 Emitter Name (UID 75)
radar.emitter_function = 2        # Emitter System Function (UID 76)
radar.emitter_number = 1
radar.location = Vector3(0, 0, -6.5)  # antenna, metres in the entity's body axes

var beam := DisElectromagneticEmissionPdu.make_beam()
beam.beam_id = 1
beam.frequency = 3.0e9            # Hz
beam.effective_radiated_power = 80.0  # dBm
beam.azimuth_sweep = PI           # radians
beam.function = 2                 # Beam Function (UID 78)
beam.track_jam = [{"entity_id": Vector3i(2, 1, 6), "emitter_number": 0, "beam_number": 0}]
radar.beams = [beam]

var pdu := DisElectromagneticEmissionPdu.new()
pdu.emitting_entity_id = $Ship/DisEntity.entity_id
pdu.event_id = Vector3i(1, 1, 7)
pdu.state_update_indicator = DisElectromagneticEmissionPdu.STATE_CHANGED_DATA
pdu.systems = [radar]
$DisNetwork.send_pdu(pdu)
```

| System key | | Beam key | |
|---|---|---|---|
| `emitter_name` | 16-bit enumeration | `beam_id`, `parameter_index` | beam number, index into a database of beam parameters |
| `emitter_function` | 8-bit enumeration | `frequency`, `frequency_range` | Hz |
| `emitter_number` | 8-bit | `effective_radiated_power` | dBm |
| `location` | `Vector3`, m, body axes | `pulse_repetition_frequency`, `pulse_width` | Hz, µs |
| `beams` | `Array` of beams | `azimuth_center`, `azimuth_sweep`, `elevation_center`, `elevation_sweep` | radians |
| | | `sweep_sync` | percent (0–100) |
| | | `function`, `high_density_track_jam`, `status` | 8-bit each (status bit 0: beam active / deactivated) |
| | | `jamming_technique` | 32-bit: kind, category, subcategory, specific (one byte each) |
| | | `track_jam` | `Array` of `{"entity_id": Vector3i, "emitter_number", "beam_number"}` |

Floating-point values travel as 32-bit floats. The length fields of the systems and beams (in 32-bit words, 0 when
a record is longer than 255 words, as the standard says) are written by `encode()`; `decode()` follows the system,
beam and target counts. Layout: 28 bytes plus 20 per system, 52 per beam and 8 per tracked or jammed target.

## Designator
A laser spot on an entity or a place: `designating_entity_id`, `designated_entity_id`, `code_name`,
`designator_code`, `power` (W), `wavelength` (µm), `spot_offset` (m, in the designated entity's body axes),
`spot_location_ecef`, and dead reckoning of the spot (`dr_algorithm`, `spot_linear_acceleration`). 88 bytes.

## IFF
Layer 1 (60 bytes) is decoded into properties: the System ID (`system_type`, `system_name`, `system_mode`,
`change_options`), `system_designator`, `system_specific_data` and the fundamental operational data
(`system_status`, `data_field_1` = alternate parameter 4, `information_layers` = which layers follow,
`data_field_2` = modifier, and `parameters`: the six 16-bit mode parameters, Mode 1, 2, 3/A, 4, C and 5 / S, each
with its on/off and damage bits as the standard defines them).

```gdscript
var iff := DisIffPdu.new()
iff.emitting_entity_id = $Aircraft/DisEntity.entity_id
iff.system_type = 1          # Mark X/XII/ATCRBS Transponder (SISO-REF-010 UID 82)
iff.system_name = 2          # Generic Mark XII (UID 83)
iff.system_mode = 3          # Normal (UID 84)
iff.system_status = 0x0F     # on, parameters 1-3 capable
iff.parameters = PackedInt32Array([0x2034, 0, 0x2777, 0, 0x2190, 0])
```

Further layers (Mode 5, Mode S, ...) are kept as raw `PackedByteArray` records in `layers`, each with its 4-byte
layer header (layer number, layer specific information, length in bytes), so a received PDU goes back out
unchanged. Setting `information_layers` to match them is up to the caller.

## Receiver and Entity State Update
Two more PDU types belong to other families:

- **Receiver** (type 27, radio family): `DisReceiverPdu` with the receiver (`radio_reference_id`,
  `radio_number`), `receiver_state` (off / on, not receiving / on and receiving), `received_power` in dBm and the
  transmitter it hears (`transmitter_entity_id`, `transmitter_radio_number`). 36 bytes; signal
  `receiver_received`. Open-DIS Python 1.1.0 writes a shorter layout without the receiver's own ID and number
  ([interoperability](interoperability.md), note 7); godot-dis follows IEEE 1278.1-2012.
- **Entity State Update** (type 67): the changing part of an Entity State PDU (location, velocity, orientation,
  appearance, variable parameters). `DisEntityManager` applies it to an entity already announced by an Entity
  State PDU and keeps that PDU's type, marking and dead reckoning parameters; it ignores updates for entities it
  has not seen. Signal `entity_state_update_received`.
