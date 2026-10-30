# Entity State PDU (DIS 7)

Layout with no variable parameters: 144 bytes, big-endian. Sources: open-dis-spec-xml
(`xml/EntityInformationFamilyPdus.xml`), open-dis-cpp `src/dis7/EntityStatePdu.cpp`, and verified by encoding with
open-dis-cpp and decoding with Open-DIS Python (`tools/dump_probe.gd` + `tools/inspect_pdu.py`).
The IEEE text is not reproduced here.

| Offset | Size | Field | Type / notes |
|---:|---:|---|---|
| 0 | 1 | Protocol version | 7 = DIS 7 (IEEE 1278.1-2012), 6 = DIS 6 |
| 1 | 1 | Exercise ID | receivers usually filter on it |
| 2 | 1 | PDU type | 1 = Entity State |
| 3 | 1 | Protocol family | 1 = Entity Information |
| 4 | 4 | Timestamp | units of 3600 s / 2^31 past the hour; low bit 1 = absolute, 0 = relative |
| 8 | 2 | Length | total PDU length in bytes |
| 10 | 1 | PDU status | DIS 7 bit field (e.g. transferred entity, LVC indicator) |
| 11 | 1 | Padding | 0 |
| 12 | 2 | Entity ID: site | |
| 14 | 2 | Entity ID: application | |
| 16 | 2 | Entity ID: entity number | |
| 18 | 1 | Force ID | 0 other, 1 friendly, 2 opposing, 3 neutral, ... (SISO-REF-010) |
| 19 | 1 | Number of variable parameters | N |
| 20 | 8 | Entity type | kind (1), domain (1), country (2), category (1), subcategory (1), specific (1), extra (1) |
| 28 | 8 | Alternative entity type | same layout; what other simulations should display (deception) |
| 36 | 12 | Linear velocity | 3 × float32, m/s, world (ECEF) frame |
| 48 | 24 | Location | 3 × float64, m, WGS84 geocentric (ECEF) |
| 72 | 12 | Orientation | 3 × float32 Euler angles psi, theta, phi in radians (convention: Phase 3) |
| 84 | 4 | Appearance | 32-bit field, meaning depends on entity kind/domain |
| 88 | 40 | Dead reckoning parameters | algorithm (1), other parameters (15), linear acceleration 3 × float32, angular velocity 3 × float32 |
| 128 | 12 | Marking | character set (1, 1 = ASCII) + 11 characters, zero padded |
| 140 | 4 | Capabilities | 32-bit field |
| 144 | 16 × N | Variable parameters | articulated / attached parts, ... |

Notes
- open-dis-cpp does not fill in the length field during `marshal()`; set it from `getMarshalledSize()`.
- In open-dis-cpp the entity ID is `EntityID { SimulationAddress { site, application }, entityNumber }`.
