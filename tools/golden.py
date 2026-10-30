"""Generates golden Entity State PDUs with the Open-DIS Python oracle.

Usage: tools/.venv/Scripts/python tools/golden.py
Writes tests/golden/espdu_<NN>_<name>.{bin,json} and tests/golden/index.json.
"""
import io
import json
import math
import random
import struct
from pathlib import Path

from opendis.dis7 import (DeadReckoningParameters, EntityIdentifier, EntityMarking, EntityStatePdu, EntityType,
                          EulerAngles, SimulationAddress, Vector3Float, VariableParameter, WorldCoordinates)
from opendis.PduFactory import createPdu
from opendis.stream import DataOutputStream

OUT = Path(__file__).resolve().parent.parent / "tests" / "golden"
rng = random.Random(1278)


def f32(v):
    return struct.unpack(">f", struct.pack(">f", v))[0]


def geodetic_to_ecef(lat_deg, lon_deg, alt):
    a = 6378137.0
    f = 1 / 298.257223563
    e2 = f * (2 - f)
    lat, lon = math.radians(lat_deg), math.radians(lon_deg)
    n = a / math.sqrt(1 - e2 * math.sin(lat) ** 2)
    x = (n + alt) * math.cos(lat) * math.cos(lon)
    y = (n + alt) * math.cos(lat) * math.sin(lon)
    z = (n * (1 - e2) + alt) * math.sin(lat)
    return [x, y, z]


def marking(text=b""):
    chars = list(text[:11]) + [0] * (11 - len(text[:11]))
    return chars


def vp_ser(record_type, f1, f2, f3, f4):
    """16-byte record exactly as opendis serializes it: u8, f64, u32, u16, u8 (big-endian)."""
    return struct.pack(">BdIHB", record_type, f1, f2, f3, f4)


def case(**kw):
    d = dict(protocol_version=7, exercise_id=1, timestamp=0, pdu_status=0, length=144,
             entity_id=[0, 0, 0], force_id=0, entity_type=[0] * 7, alternative_entity_type=[0] * 7,
             linear_velocity=[0.0] * 3, location_ecef=[0.0] * 3, orientation=[0.0] * 3, appearance=0,
             dr_algorithm=0, dr_parameters=[0] * 15, dr_linear_acceleration=[0.0] * 3,
             dr_angular_velocity=[0.0] * 3, marking_character_set=1, marking=[0] * 11, capabilities=0,
             variable_parameters=[])
    d.update(kw)
    return d


def build(c):
    """Builds the oracle PDU from a case dict, returns bytes. Sets c['length']."""
    p = EntityStatePdu()
    p.protocolVersion = c["protocol_version"]
    p.exerciseID = c["exercise_id"]
    p.timestamp = c["timestamp"]
    p.pduStatus = c["pdu_status"]
    s, a, n = c["entity_id"]
    p.entityID = EntityIdentifier(SimulationAddress(s, a), n)
    p.forceId = c["force_id"]
    p.entityType = EntityType(*c["entity_type"])
    p.alternativeEntityType = EntityType(*c["alternative_entity_type"])
    p.entityLinearVelocity = Vector3Float(*c["linear_velocity"])
    p.entityLocation = WorldCoordinates(*c["location_ecef"])
    p.entityOrientation = EulerAngles(*c["orientation"])
    p.entityAppearance = c["appearance"]
    p.deadReckoningParameters = DeadReckoningParameters(
        c["dr_algorithm"], list(c["dr_parameters"]), Vector3Float(*c["dr_linear_acceleration"]),
        Vector3Float(*c["dr_angular_velocity"]))
    # opendis writes marking characters as SIGNED bytes: pass values -128..127.
    p.marking = EntityMarking(c["marking_character_set"], [b - 256 if b > 127 else b for b in c["marking"]])
    p.capabilities = c["capabilities"]
    vps = []
    for h in c["variable_parameters"]:
        r, f1, f2, f3, f4 = struct.unpack(">BdIHB", bytes.fromhex(h))
        vps.append(VariableParameter(r, f1, f2, f3, f4))
    p.variableParameters = vps
    p.length = 144 + 16 * len(vps)  # opendis does not fill the header length itself
    buf = io.BytesIO()
    p.serialize(DataOutputStream(buf))
    c["length"] = p.length
    return buf.getvalue()


def normalize(c):
    for k in ("linear_velocity", "orientation", "dr_linear_acceleration", "dr_angular_velocity"):
        c[k] = [f32(v) for v in c[k]]
    c["location_ecef"] = [float(v) for v in c["location_ecef"]]
    return c


def decoded_dict(p):
    e = p.entityID

    def tt(x):
        return [x.entityKind, x.domain, x.country, x.category, x.subcategory, x.specific, x.extra]

    def v3(x):
        return [x.x, x.y, x.z]

    dr = p.deadReckoningParameters
    return dict(
        protocol_version=p.protocolVersion, exercise_id=p.exerciseID, timestamp=p.timestamp,
        pdu_status=p.pduStatus, length=p.length,
        entity_id=[e.simulationAddress.site, e.simulationAddress.application, e.entityNumber],
        force_id=p.forceId, entity_type=tt(p.entityType), alternative_entity_type=tt(p.alternativeEntityType),
        linear_velocity=v3(p.entityLinearVelocity), location_ecef=v3(p.entityLocation),
        orientation=[p.entityOrientation.psi, p.entityOrientation.theta, p.entityOrientation.phi],
        appearance=p.entityAppearance, dr_algorithm=dr.deadReckoningAlgorithm,
        dr_parameters=list(dr.parameters), dr_linear_acceleration=v3(dr.entityLinearAcceleration),
        dr_angular_velocity=v3(dr.entityAngularVelocity),
        marking_character_set=p.marking.characterSet, marking=[b & 0xFF for b in p.marking.characters],
        capabilities=p.capabilities,
        variable_parameters=[vp_ser(v.recordType, v.variableParameterFields1, v.variableParameterFields2,
                                    v.variableParameterFields3, v.variableParameterFields4).hex()
                             for v in p.variableParameters])


def rand_vp():
    return vp_ser(rng.choice([0, 1, 2, 3]), rng.uniform(-1e3, 1e3), rng.getrandbits(32),
                  rng.getrandbits(16), rng.getrandbits(8)).hex()


def rand_type():
    return [rng.randrange(256), rng.randrange(256), rng.getrandbits(16)] + [rng.randrange(256) for _ in range(4)]


def rand_case():
    return case(
        exercise_id=rng.randrange(256), timestamp=rng.getrandbits(32), pdu_status=rng.randrange(256),
        entity_id=[rng.getrandbits(16) for _ in range(3)], force_id=rng.randrange(256),
        entity_type=rand_type(), alternative_entity_type=rand_type(),
        linear_velocity=[rng.uniform(-500, 500) for _ in range(3)],
        location_ecef=[rng.uniform(-7e6, 7e6) for _ in range(3)],
        orientation=[rng.uniform(-math.pi, math.pi) for _ in range(3)],
        appearance=rng.getrandbits(32), dr_algorithm=rng.randrange(10),
        dr_parameters=[rng.randrange(256) for _ in range(15)],
        dr_linear_acceleration=[rng.uniform(-50, 50) for _ in range(3)],
        dr_angular_velocity=[rng.uniform(-3, 3) for _ in range(3)],
        marking_character_set=rng.randrange(256), marking=[rng.randrange(256) for _ in range(11)],
        capabilities=rng.getrandbits(32), variable_parameters=[rand_vp() for _ in range(rng.randrange(0, 4))])


def cases():
    out = []

    def add(name, **kw):
        out.append((name, case(**kw)))

    add("default")
    add("friendly_tank_ankara", exercise_id=1, timestamp=1234567 * 2 + 1, entity_id=[1, 2, 100], force_id=1,
        entity_type=[1, 1, 225, 1, 1, 3, 0], alternative_entity_type=[1, 1, 225, 1, 1, 3, 0],
        location_ecef=geodetic_to_ecef(39.93, 32.86, 900.0), orientation=[1.2, 0.01, -0.02],
        appearance=0x00000001, dr_algorithm=2, marking=marking(b"TANK-01"))
    add("hostile_aircraft", exercise_id=2, timestamp=987654322, entity_id=[7, 9, 4242], force_id=2,
        entity_type=[1, 2, 71, 1, 4, 0, 0], alternative_entity_type=[1, 2, 222, 1, 1, 0, 0],
        linear_velocity=[-150.5, 220.25, 30.125], location_ecef=geodetic_to_ecef(40.1, 33.2, 8000.0),
        orientation=[2.5, 0.15, -0.3], appearance=0x00030000, dr_algorithm=5,
        dr_linear_acceleration=[0.5, -0.25, 9.81], dr_angular_velocity=[0.01, 0.02, -0.03],
        marking=marking(b"BANDIT"), capabilities=0x0000000F)
    add("extreme_max_ints", exercise_id=255, timestamp=0xFFFFFFFF, pdu_status=255, entity_id=[65535] * 3,
        force_id=255, entity_type=[255, 255, 65535, 255, 255, 255, 255],
        alternative_entity_type=[255, 255, 65535, 255, 255, 255, 255], appearance=0xFFFFFFFF,
        dr_algorithm=255, dr_parameters=[255] * 15, capabilities=0xFFFFFFFF, marking=[255] * 11)
    add("extreme_doubles", location_ecef=[-1.7976931348623157e308, 1e300, -4.9e-324],
        linear_velocity=[-1e3, 1e3, 0.1], orientation=[-math.pi, math.pi, 1e-5])
    add("extreme_float32", linear_velocity=[3.4e38, -3.4e38, 1e-40],
        orientation=[-0.0, 1.17549435e-38, 3.4028234e38],
        dr_linear_acceleration=[-0.0, 1e-40, -3.4e38], dr_angular_velocity=[3.4e38, -1e-40, 0.0],
        location_ecef=[-0.0, 6378137.0, -6356752.314245179])
    add("marking_11_chars", marking=marking(b"ABCDEFGHIJK"))
    add("marking_empty", marking=marking(b""))
    add("marking_nonascii", marking=[0x80, 0x81, 0xA0, 0xC3, 0xA9, 0xFE, 0xFF, 0x7F, 0x00, 0x90, 0xFF])
    for alg in range(10):
        add(f"dr_algorithm_{alg}", dr_algorithm=alg, dr_parameters=[(alg * 17 + i * 13 + 1) % 256 for i in range(15)],
            dr_linear_acceleration=[0.5 * alg + 0.25, -1.5, 2.0 + alg], dr_angular_velocity=[0.1 * alg, -0.2, 0.3],
            linear_velocity=[10.0 + alg, -5.0, 1.0],
            location_ecef=geodetic_to_ecef(39.9 + alg * 0.1, 32.8, 100.0 * alg))
    add("exercise_0", exercise_id=0, entity_id=[1, 1, 1], force_id=3)
    add("exercise_255", exercise_id=255, entity_id=[1, 1, 2], force_id=1)
    add("pdu_status_nonzero", pdu_status=0b10110101, entity_id=[3, 3, 3])
    add("timestamp_absolute", timestamp=0x12345679, entity_id=[4, 4, 4])
    add("timestamp_relative", timestamp=0x12345678, entity_id=[4, 4, 5])
    add("vp_one_articulated", entity_id=[5, 5, 5], appearance=0x20,
        variable_parameters=[vp_ser(0, 1.5, 0xDEADBEEF, 0xCAFE, 0x7F).hex()])
    add("vp_three_mixed", entity_id=[5, 5, 6],
        variable_parameters=[vp_ser(0, 3.14159, 4096, 1, 2).hex(), vp_ser(1, -2.5e10, 0x01020304, 0x0506, 0x07).hex(),
                             vp_ser(0, 0.0, 0xFFFFFFFF, 0xFFFF, 0xFF).hex()])
    for i in range(5):
        out.append((f"random_{i}", rand_case()))
    return out


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for f in list(OUT.glob("espdu_*")):
        f.unlink()
    names = []
    for i, (name, c) in enumerate(cases()):
        normalize(c)
        data = build(c)
        stem = f"espdu_{i:02d}_{name}"
        assert len(data) == 144 + 16 * len(c["variable_parameters"]), stem
        (OUT / f"{stem}.bin").write_bytes(data)
        # Godot's JSON parser does not round-trip every double exactly, so the location is also
        # stored as big-endian IEEE 754 bits (16 hex digits per coordinate).
        out = dict(c, location_ecef_bits=[struct.pack(">d", v).hex() for v in c["location_ecef"]])
        (OUT / f"{stem}.json").write_text(json.dumps(out, indent=2) + "\n")
        # Self-check: decode the written bytes and compare with the written JSON.
        got = decoded_dict(createPdu(data))
        want = json.loads((OUT / f"{stem}.json").read_text())
        want.pop("location_ecef_bits")
        for k, v in want.items():
            assert got[k] == v, (stem, k, got[k], v)
        names.append(stem)
    (OUT / "index.json").write_text(json.dumps(names, indent=2) + "\n")
    print(f"wrote {len(names)} cases to {OUT}")


main()
