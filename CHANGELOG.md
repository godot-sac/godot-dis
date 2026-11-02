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
- Demo: two-sided exercise around Üsküdar, Istanbul plus civilian traffic from Open-DIS Python (`--port`; traffic `--start`,
  `--rate`, dead reckoning algorithm 4 with turn rate, racetrack routes); `tools/link_milsymbol.py`.
- CI for Windows, Linux and macOS.
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
- Release zip: `tools/package_addon.py` packs `godot-dis-<version>.zip` (`addons/dis/` with the Windows, Linux and
  macOS libraries, data, icons, `LICENSE`, `THIRD_PARTY_NOTICES.md`); `tools/check_package.py` tries it in a new,
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
- Demo scenario moved from Ankara to Üsküdar, Istanbul (origin 41.0200 N, 29.0500 E, ellipsoid height 0, the same
  as the godot-geo Üsküdar terrain packages): land routes on main roads of the Asian side, a blue patrol craft and a
  civilian ferry on the Bosphorus, the airliner at 3 km (first turn after ~310 s); `DemoUnitMover.surface` (stay on
  the ground or sea surface when there is one) and `DemoUnitMover.racetrack()`.

### Fixed
- Linux: loading godot-dis and godot-jsbsim in one project crashed (signal 11 in JSBSim's `std::regex`): the
  library exported its statically linked libstdc++ (~4400 symbols). It now exports only its entry point
  (`src/linux_exports.map`), checked by `tools/check_exports.py` in CI.
- The demo no longer shows the full local path of a recording on screen.
- The first headless import of a new project (`--import`, `--editor --quit`) no longer crashes on exit: works
  around godotengine/godot#111048 by unregistering the classes early when a headless editor loads the
  extension mid-session (`src/editor_first_load_guard.cpp`).
- Smoothing started from the previous frame's pose, so fast entities stalled for a frame and caught up on every
  update (visible as jitter on the demo airliner).
