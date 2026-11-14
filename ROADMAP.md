# Roadmap

## Status
0.2.0 (2026-11-09): 19 PDU types (Entity State and Entity State Update, Fire, Detonation, Collision, simulation
management, Transmitter, Signal, Receiver, Electromagnetic Emission, Designator, IFF) checked byte for byte against Open-DIS Python, opendis7-java and Wireshark; networking,
coordinates, dead reckoning DRM 1–5, entity publishing and remote entities, exercise control, radio monitor,
recording and playback (pcapng, PDU log), enumerations, class reference in the editor (F1), release zip. See [CHANGELOG.md](CHANGELOG.md) and
[compatibility](docs/compatibility.md).

## Not tried yet
- The macOS libraries on a Mac (CI builds Windows and Linux only) and two computers on a LAN.

## Next
- Timestamps: a choice between absolute and relative time for sent PDUs; an option to use received timestamps
  for dead reckoning in time-synchronised exercises.
- Networking: receive on a thread (or a larger receive buffer) for exercises with many entities and long frames.

## Later
- Linux ARM64 and other platforms Godot exports to.
- Godot Asset Library / Asset Store listing.
- An HLA gateway stays out of scope for this addon (see the README).
