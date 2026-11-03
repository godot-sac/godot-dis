# Roadmap

## Status
v0.1.0 is in preparation. Done: 14 PDU types (Entity State, Fire, Detonation, Collision, simulation management,
Transmitter, Signal) checked byte for byte against Open-DIS Python, opendis7-java and Wireshark; networking,
coordinates, dead reckoning DRM 1–5, entity publishing and remote entities, exercise control, radio monitor,
recording and playback (pcapng, PDU log), enumerations, release zip. See [CHANGELOG.md](CHANGELOG.md) and
[compatibility](docs/compatibility.md).

## Before and right after the first release
- First CI run on GitHub (Windows, Linux, macOS); the release zip built by CI with the macOS libraries.
- Try the macOS libraries on a Mac, Godot 4.3–4.6, an exported game (export templates) and two computers on a LAN.

## Next (0.2)
- PDUs: Entity State Update, Receiver, Electromagnetic Emission, Designator, IFF.
- Dead reckoning: the body-axis algorithms DRM 6–9 exactly (they are approximated by their world-axis
  counterparts 2–5 today).
- Typed articulated and attached parts (turret, gun, ...) on top of `variable_parameters`.
- Timestamps: a choice between absolute and relative time for sent PDUs; an option to use received timestamps
  for dead reckoning in time-synchronised exercises.
- Class reference in the editor (Godot `doc_classes` XML), so F1 on a `Dis*` node shows its documentation.
- Networking: receive on a thread (or a larger receive buffer) for exercises with many entities and long frames.

## Later
- Linux ARM64 and other platforms Godot exports to.
- Godot Asset Library / Asset Store listing.
- An HLA gateway stays out of scope for this addon (see the README).
