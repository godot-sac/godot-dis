# godot-dis

DIS (IEEE 1278.1, Distributed Interactive Simulation) for Godot 4 — send and receive Entity State PDUs,
convert between WGS84 / ECEF and Godot coordinates, and dead-reckon remote entities.
A C++ GDExtension built on [Open-DIS](https://github.com/open-dis/open-dis-cpp) (BSD-2-Clause) and godot-cpp.

> 🚧 Work in progress. See [PLAN.md](PLAN.md).

## Goals (v0.1.0)
- DIS 7 Entity State PDU encode/decode (reads DIS 6 too), verified byte-for-byte against Open-DIS
- UDP broadcast / unicast / multicast networking node
- WGS84 geodetic ↔ ECEF ↔ local ENU ↔ Godot conversions, DIS Euler angles ↔ `Basis`
- Dead reckoning (DRM 1–5), heartbeat and threshold-based publishing, timeouts
- Local and remote entity nodes, entity type → scene mapping
- Works with [godot-milsymbol](../godot-milsymbol) for map symbols

## License
MIT. Enumeration data derived from SISO-REF-010 is used under SISO's terms —
see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
