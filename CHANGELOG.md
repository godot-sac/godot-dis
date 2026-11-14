# Changelog

All notable changes to this project are documented here. Versions follow [semver](https://semver.org/).

## [Unreleased]
### Added
- Articulated and attached parts: `DisVariableParameter` builds and reads the 16-byte variable parameter records
  (turret azimuth, gun elevation, rotor rotation, stores on stations; record types, metrics), `DisEntity`
  `variable_parameters` are sent with every PDU and at once when they change. SISO tables
  `articulated_part_class`, `articulated_part_metric`, `attached_part` and `variable_parameter_record_type` in
  `DisEnums`. Checked against the standard's layout (`tools/vp_golden.py`) and Wireshark.

### Fixed
- Entity State and Detonation: a variable parameter that is not 16 bytes now fails the encode with an error (like
  Entity State Update) instead of being left out, which made `get_length()` larger than the encoded PDU.
- Signal: a `data_length` that does not fit the 16-bit field fails the encode instead of being cut silently.
- `DisPdu.get_last_error()` is cleared by every decode, also when the next packet fails before header validation.
- Comment PDU: the inherited `request_id` and `padding` (it has neither) are hidden in the Inspector.
### Changed
- Dead reckoning: the body-axis algorithms DRM 6–9 are now exact (IEEE 1278.1 Annex E): the velocity and the
  body-axis acceleration turn with the entity (R₁ / R₂ closed forms), so a steady turn is predicted as a circle.
  Before they were extrapolated like 2–5. `DisEntity` sends DRM 8 / 9 acceleration as the rate of change of the
  body-axis velocity (without the centripetal part). Checked against numerical integration (docs/entities.md).
### Documentation
- Exported games tested with Godot 4.7.2 release templates on Windows and Linux (docs/compatibility.md).

## [0.2.0] - 2026-11-09
### Added
- Five PDU types (19 in all): Entity State Update (`DisEntityStateUpdatePdu`; `DisEntityManager` applies it to known
  entities), Receiver (`DisReceiverPdu`), Electromagnetic Emission (`DisElectromagneticEmissionPdu`, systems and
  beams as Dictionaries), Designator (`DisDesignatorPdu`) and IFF (`DisIffPdu`, layer 1 decoded, further layers
  raw), with a `DisNetwork` signal each. Checked against Open-DIS Python (Entity State Update, Electromagnetic
  Emission, Designator), the IEEE 1278.1-2012 layout (Receiver, IFF), the opendis7-java capture and Wireshark;
  [docs/emissions.md](docs/emissions.md). The own-packet filter now covers the Distributed Emission family too.
- Class reference in the editor: F1 on any `Dis*` class shows its description, methods, properties, signals and
  constants (`doc_classes/`, built into the debug library). `tools/check_docs.py` and a CI step keep it complete
  and in step with the API.

## [0.1.0] - 2026-11-08
First release.

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
- Demo: two-sided exercise around Üsküdar, Istanbul plus civilian traffic from Open-DIS Python (`--port`; traffic `--start`,
  `--rate`, dead reckoning algorithm 4 with turn rate, racetrack routes); `tools/link_milsymbol.py`.
- CI for Windows and Linux.
- Fire, Detonation, Remove Entity, Start/Resume, Stop/Freeze and Acknowledge PDUs (`DisFirePdu`,
  `DisDetonationPdu`, `DisRemoveEntityPdu`, `DisStartResumePdu`, `DisStopFreezePdu`, `DisAcknowledgePdu`) on a
  common `DisPduBase`; `DisPdu.decode()` returns any of them; byte-for-byte golden tests against Open-DIS Python.
- `DisNetwork` signals per PDU type (`fire_received`, `detonation_received`, `start_resume_received`, ...);
  `inject_packet()`; the own-packet filter covers every PDU family that starts with the sender's ID.
- `DisExerciseControl`: obeys Start/Resume, Stop/Freeze (also at a requested time) and Remove Entity with automatic
  Acknowledge and optional tree pause; sends them as a simulation manager with retries.
- `DisEntity.fire()`, `detonate()`, `create_fire_pdu()`, `create_detonation_pdu()`.
- Recording and playback: `DisRecorder` (pcapng or `.pdulog`), `DisRecording` (load / save / summary; also reads
  pcap and other tools' captures), `DisPlayer` (into the scene or onto the network, speed, pause, seek, loop),
  `DisEntityManager.time_scale`; command line tool `addons/dis/tools/dis_cli.gd` (record, play, info, dump,
  convert).
- Interoperability tests: Wireshark's DIS dissector (tshark) on our pcapng, public Wireshark and NPS
  (opendis7-java) captures (`tools/fetch_captures.py`); results in `docs/interoperability.md`, PDU support table in
  the README.
- `DisEnums`: PDU type, protocol family, warhead, fuse, detonation result, stop/freeze reason and acknowledge
  tables; `appearance_to_status()`; `entity_type_to_sidc()` accepts 2525D status digits 0–5.
- Demo: fire (F), Remove Entity request (X), stop / resume the exercise (S / R), event log, destroyed units,
  `--record`; replay viewer `demo/replay.tscn`; the Open-DIS Python traffic obeys and acknowledges exercise control.
- Collision, Create Entity, Comment, Set Data, Data, Transmitter and Signal PDUs (`DisCollisionPdu`,
  `DisCreateEntityPdu`, `DisCommentPdu`, `DisSetDataPdu`, `DisDataPdu`, `DisTransmitterPdu`, `DisSignalPdu`) with
  `DisNetwork` signals; datum helpers (`add_text()`, `get_text()`, ...); `DisExerciseControl.create_entity_requested`
  and `send_create_entity()`.
- `DisRadioMonitor`: radios on the network and who is transmitting.
- `DisPduBase.trailing_bytes`: bytes a sender adds after the known fields survive decode + encode; the received
  protocol family byte is kept too.
- Demo: radio calls (T) shown with radio waves on the map, the Open-DIS Python airliner talks on 118.1 MHz,
  Comment PDUs when the exercise is stopped or resumed.
- Release zip: `tools/package_addon.py` packs `godot-dis-<version>.zip` (`addons/dis/` with the Windows and Linux
  libraries, data, icons, `LICENSE`, `THIRD_PARTY_NOTICES.md`); `tools/check_package.py` tries it in a new,
  empty project (optionally next to other add-on zips, `--with`); CI packs and checks it on every push and makes a
  draft GitHub release on a `vX.Y.Z` tag.
- Editor icons for the nodes (`addons/dis/icons/`), project / Asset Library icon.
- Docs: [getting started](docs/getting-started.md) (empty project to two simulations and a recording in about 15
  minutes), [compatibility](docs/compatibility.md), [release](docs/release.md); README animation and social preview
  (`tools/make_media.py`, demo option `--capture`).

### Changed
- `DisEntityStatePdu` derives from `DisPduBase` (same properties and methods).
- A PDU length field of 0 is read as the datagram size (some senders never fill it in).
- `get_length()` counts `trailing_bytes`; the size of the known fields is `get_body_length()`.
- Radio Communications PDUs that carry our own site / application are ignored like other own PDUs.
- `THIRD_PARTY_NOTICES.md` (also in the release zip) carries the open-dis-cpp (BSD-2-Clause) and godot-cpp (MIT)
  license texts, as both are compiled into the libraries.
- Demo scenario moved from Ankara to Üsküdar, Istanbul (origin 41.0200 N, 29.0500 E, ellipsoid height 0, the same
  as the godot-geo Üsküdar terrain packages): land routes on main roads of the Asian side, a blue patrol craft and a
  civilian ferry on the Bosphorus, the airliner at 3 km (first turn after ~310 s); `DemoUnitMover.surface` (stay on
  the ground or sea surface when there is one) and `DemoUnitMover.racetrack()`.

### Fixed
- Linux: loading godot-dis and godot-jsbsim in one project crashed (signal 11 in JSBSim's `std::regex`): the
  library exported its statically linked libstdc++ (~4400 symbols). It now exports only its entry point
  (`src/linux_exports.map`), checked by `tools/check_exports.py` in CI.
- The demo no longer shows the full local path of a recording on screen.
- `DisNetwork` handles every PDU of a datagram that bundles several (IEEE 1278.1-2012), not only the first one;
  stopping the network from a signal handler while packets wait no longer crashes.
- `DisExerciseControl`: a request from another manager with the same request ID (site 2 / application 1 and site 0
  / application 513 shared a key) or from a restarted manager is no longer taken for a resend and ignored; a resend
  is the same request within 30 s. A `remove_entity_requested` handler may free the entity.
- `DisPlayer`, `DisRadioMonitor`: signal handlers that change the recording, seek, or clear the monitor no longer
  leave the player or monitor reading freed data. Restoring entities after a seek early in a run no longer treats
  old reports as new.
- Decoding: the variable parameter count of Entity State and Detonation PDUs must fit the PDU's length field, not
  just the datagram; `.pdulog` lines with a NaN or infinite time are rejected.
- Encoding refuses values that do not fit their length fields (PDU over 65535 bytes, more than 255 variable
  parameters or modulation parameter bytes, Signal data over 8191 bytes) instead of wrapping silently.
- `DisEntityManager` ignores Entity State PDUs with NaN or infinite values (one warning), and creates at most
  `max_entities` (10000) remote entities: PDUs of further new entities are dropped (`get_dropped_count()`, one
  warning, `entity_limit_reached` signal), so a sender cannot make it create nodes without end. `DisRadioMonitor`
  likewise tracks at most `max_radios` (1000).
- Body-axis dead reckoning (DRM 6–9): `DisEntity` sends the acceleration in body axes and `DisEntityManager` reads
  it that way (both used ECEF).
- A fresh clone warned about invalid UIDs in GUT's scenes on every editor start: GUT's `.import` files are now
  tracked.
- The first headless import of a new project (`--import`, `--editor --quit`) no longer crashes on exit: works
  around godotengine/godot#111048 by unregistering the classes early when a headless editor loads the
  extension mid-session (`src/editor_first_load_guard.cpp`).
- Smoothing started from the previous frame's pose, so fast entities stalled for a frame and caught up on every
  update (visible as jitter on the demo airliner).
