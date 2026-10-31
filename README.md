# godot-dis

DIS (IEEE 1278.1, Distributed Interactive Simulation) for Godot 4 — send and receive Entity State PDUs,
convert between WGS84 / ECEF and Godot coordinates, and dead-reckon remote entities.
A C++ GDExtension built on [Open-DIS](https://github.com/open-dis/open-dis-cpp) (BSD-2-Clause) and godot-cpp.

> 🚧 Work in progress (not released yet). See [PLAN.md](PLAN.md).

![Demo: two Godot simulations and Open-DIS Python on one tactical map](docs/images/demo_blue.png)

## Features
- DIS 7 Entity State PDU encode/decode (reads DIS 6 too), verified byte-for-byte against Open-DIS Python
  and in Wireshark — [docs/entity-state-pdu.md](docs/entity-state-pdu.md)
- `DisNetwork`: UDP broadcast / unicast / multicast, exercise and own-packet filters, several DIS applications
  sharing one port — [docs/networking.md](docs/networking.md)
- `DisGeoOrigin`, `DisGeo`: WGS84 geodetic ↔ ECEF ↔ local ENU ↔ Godot, DIS Euler angles ↔ `Basis`, in doubles
  — [docs/coordinates.md](docs/coordinates.md)
- `DisEntity` (publish a node: thresholds, heartbeat), `DisEntityManager` (remote entities: dead reckoning DRM 1–5,
  smoothing, timeouts), entity type → scene mapping — [docs/entities.md](docs/entities.md)
- `DisEnums`: SISO-REF-010 names and DIS entity type → MIL-STD-2525D symbol codes for
  [godot-milsymbol](../godot-milsymbol) — [docs/enumerations.md](docs/enumerations.md)
- Demo: a two-sided exercise around Ankara — [docs/demo.md](docs/demo.md)
- Builds on Windows, Linux and macOS (GitHub Actions) — [docs/building.md](docs/building.md)

## License
MIT. Enumeration data derived from SISO-REF-010 is used under SISO's terms (reprinted with permission from
SISO Inc.) — see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
