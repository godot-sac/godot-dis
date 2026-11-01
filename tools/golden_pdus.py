"""Generates golden PDUs of every type except Entity State (see golden.py) with the Open-DIS Python oracle.

Usage: tools/.venv/Scripts/python tools/golden_pdus.py
Writes tests/golden/pdus/<type>_<NN>_<name>.{bin,json} and tests/golden/pdus/index.json.

Each JSON names the Godot class and its expected property values, so one generic GUT test
(tests/unit/test_pdus_golden.gd) checks decode (bytes -> fields) and encode (fields -> identical bytes):
  {"class": "DisFirePdu", "length": 96, "fields": {"exercise_id": 1, "event_id": [1, 2, 3], ...}}
Values: ints and int lists as they are; float vectors as lists of float32 values; doubles as
{"f64": "<16 hex digits, big-endian IEEE bits>"} or {"f64_list": [...]} because Godot's JSON parser can be off in
the last bit; raw 16-byte variable parameters as {"raw_hex": [...]}.
"""
import io
import json
import math
import random
import struct
from pathlib import Path

from opendis.dis7 import (AcknowledgePdu, ClockTime, DetonationPdu, EntityIdentifier, EntityType, FirePdu,
                          MunitionDescriptor, RemoveEntityPdu, SimulationAddress, StartResumePdu, StopFreezePdu,
                          VariableParameter, Vector3Float, WorldCoordinates)
from opendis.PduFactory import createPdu
from opendis.record.common import EventIdentifier
from opendis.stream import DataOutputStream

OUT = Path(__file__).resolve().parent.parent / "tests" / "golden" / "pdus"
rng = random.Random(1278 + 2)

U16, U32 = 0xFFFF, 0xFFFFFFFF


def f32(v):
    return struct.unpack(">f", struct.pack(">f", v))[0]


def f64(v):
    return {"f64": struct.pack(">d", v).hex()}


def f64_list(values):
    return {"f64_list": [struct.pack(">d", v).hex() for v in values]}


def geodetic_to_ecef(lat_deg, lon_deg, alt):
    a = 6378137.0
    f = 1 / 298.257223563
    e2 = f * (2 - f)
    lat, lon = math.radians(lat_deg), math.radians(lon_deg)
    n = a / math.sqrt(1 - e2 * math.sin(lat) ** 2)
    return [(n + alt) * math.cos(lat) * math.cos(lon), (n + alt) * math.cos(lat) * math.sin(lon),
            (n * (1 - e2) + alt) * math.sin(lat)]


def signed32(v):
    return v - (1 << 32) if v >= 1 << 31 else v


def eid(v):
    return EntityIdentifier(SimulationAddress(v[0], v[1]), v[2])


def evid(v):
    return EventIdentifier(SimulationAddress(v[0], v[1]), v[2])


def serialize(pdu, length):
    pdu.length = length  # opendis does not fill the header length itself
    buf = io.BytesIO()
    pdu.serialize(DataOutputStream(buf))
    data = buf.getvalue()
    assert len(data) == length, (type(pdu).__name__, len(data), length)
    return data


def header(pdu, c):
    pdu.protocolVersion = c.get("protocol_version", 7)
    pdu.exerciseID = c.get("exercise_id", 1)
    pdu.timestamp = c.get("timestamp", 0)
    pdu.pduStatus = c.get("pdu_status", 0)


def header_fields(c):
    return {k: c.get(k, d) for k, d in
            (("protocol_version", 7), ("exercise_id", 1), ("timestamp", 0), ("pdu_status", 0))}


def rand_id():
    return [rng.getrandbits(16) for _ in range(3)]


def rand_header():
    return dict(exercise_id=rng.randrange(256), timestamp=rng.getrandbits(32), pdu_status=rng.randrange(256))


def rand_type():
    return [rng.randrange(256), rng.randrange(256), rng.getrandbits(16)] + [rng.randrange(256) for _ in range(4)]


def vp_bytes(record_type, f1, f2, f3, f4):
    """16-byte record exactly as opendis serializes it: u8, f64, u32, u16, u8 (big-endian)."""
    return struct.pack(">BdIHB", record_type, f1, f2, f3, f4)


# ---------------------------------------------------------------------------------------------------------------
# Simulation management

def sim_ids(pdu, c):
    pdu.originatingEntityID = eid(c.get("originating_id", [0, 0, 0]))
    pdu.receivingEntityID = eid(c.get("receiving_id", [0, 0, 0]))
    return {"originating_id": c.get("originating_id", [0, 0, 0]), "receiving_id": c.get("receiving_id", [0, 0, 0]),
            "request_id": c.get("request_id", 0)}


def start_resume(c):
    p = StartResumePdu()
    header(p, c)
    fields = header_fields(c) | sim_ids(p, c)
    rw, sim = c.get("real_world_time", [0, 0]), c.get("simulation_time", [0, 0])
    p.realWorldTime = ClockTime(rw[0], rw[1])
    p.simulationTime = ClockTime(sim[0], sim[1])
    p.requestID = c.get("request_id", 0)
    fields |= {"real_world_time_hour": signed32(rw[0]), "real_world_time_past_hour": rw[1],
               "simulation_time_hour": signed32(sim[0]), "simulation_time_past_hour": sim[1]}
    return "DisStartResumePdu", serialize(p, 44), fields


def stop_freeze(c):
    p = StopFreezePdu()
    header(p, c)
    fields = header_fields(c) | sim_ids(p, c)
    rw = c.get("real_world_time", [0, 0])
    p.realWorldTime = ClockTime(rw[0], rw[1])
    p.reason = c.get("reason", 0)
    p.frozenBehavior = c.get("frozen_behavior", 0)
    pad = c.get("padding", 0)
    p.padding1 = pad - 0x10000 if pad >= 0x8000 else pad  # opendis writes this padding as a signed short
    p.requestID = c.get("request_id", 0)
    fields |= {"real_world_time_hour": signed32(rw[0]), "real_world_time_past_hour": rw[1],
               "reason": p.reason, "frozen_behavior": p.frozenBehavior, "padding": pad}
    return "DisStopFreezePdu", serialize(p, 40), fields


def acknowledge(c):
    p = AcknowledgePdu()
    header(p, c)
    fields = header_fields(c) | sim_ids(p, c)
    p.acknowledgeFlag = c.get("acknowledge_flag", 0)
    p.responseFlag = c.get("response_flag", 0)
    p.requestID = c.get("request_id", 0)
    fields |= {"acknowledge_flag": p.acknowledgeFlag, "response_flag": p.responseFlag}
    return "DisAcknowledgePdu", serialize(p, 32), fields


def remove_entity(c):
    p = RemoveEntityPdu()
    header(p, c)
    fields = header_fields(c) | sim_ids(p, c)
    p.requestID = c.get("request_id", 0)
    return "DisRemoveEntityPdu", serialize(p, 28), fields


def unix_clock(seconds, absolute=True):
    hours = math.floor(seconds / 3600.0)
    past = int((seconds - hours * 3600.0) / 3600.0 * 2 ** 31)
    return [hours, (past << 1) | (1 if absolute else 0)]


def rand_sim():
    return rand_header() | dict(originating_id=rand_id(), receiving_id=rand_id(), request_id=rng.getrandbits(32))


def sim_cases():
    out = []
    add = lambda kind, name, **kw: out.append((kind, name, kw))  # noqa: E731
    everyone = [U16, U16, U16]
    maxed = dict(exercise_id=255, timestamp=U32, pdu_status=255, originating_id=[U16] * 3, receiving_id=[U16] * 3,
                 request_id=U32)

    add(start_resume, "default")
    add(start_resume, "start_everyone_now", originating_id=[1, 1, 0], receiving_id=everyone, request_id=1,
        real_world_time=unix_clock(1793534400.25), simulation_time=unix_clock(0.0, False))
    add(start_resume, "resume_one_application", originating_id=[1, 1, 0], receiving_id=[2, 1, 0], request_id=7,
        real_world_time=unix_clock(1793534460.5), simulation_time=unix_clock(5400.125, False))
    add(start_resume, "max_values", real_world_time=[U32, U32], simulation_time=[U32, U32], **maxed)
    add(start_resume, "negative_hour", real_world_time=[U32 - 1, 2], simulation_time=[1 << 31, 3], request_id=9)
    for i in range(3):
        add(start_resume, f"random_{i}", real_world_time=[rng.getrandbits(32), rng.getrandbits(32)],
            simulation_time=[rng.getrandbits(32), rng.getrandbits(32)], **rand_sim())

    add(stop_freeze, "default")
    add(stop_freeze, "freeze_everyone", originating_id=[1, 1, 0], receiving_id=everyone, request_id=2,
        real_world_time=unix_clock(1793534500.0), reason=1, frozen_behavior=0b010)
    for reason in range(9):
        add(stop_freeze, f"reason_{reason}", originating_id=[1, 1, 0], receiving_id=[2, U16, 0],
            request_id=100 + reason, reason=reason, frozen_behavior=reason % 8)
    add(stop_freeze, "max_values", real_world_time=[U32, U32], reason=255, frozen_behavior=255, padding=U16, **maxed)
    for i in range(3):
        add(stop_freeze, f"random_{i}", real_world_time=[rng.getrandbits(32), rng.getrandbits(32)],
            reason=rng.randrange(256), frozen_behavior=rng.randrange(256), **rand_sim())

    add(acknowledge, "default")
    for flag in range(1, 6):
        for response in range(5):
            add(acknowledge, f"flag_{flag}_response_{response}", originating_id=[2, 1, 0], receiving_id=[1, 1, 0],
                request_id=flag * 10 + response, acknowledge_flag=flag, response_flag=response)
    add(acknowledge, "max_values", acknowledge_flag=U16, response_flag=U16, **maxed)
    for i in range(3):
        add(acknowledge, f"random_{i}", acknowledge_flag=rng.getrandbits(16), response_flag=rng.getrandbits(16),
            **rand_sim())

    add(remove_entity, "default")
    add(remove_entity, "remove_tank", originating_id=[1, 1, 0], receiving_id=[2, 1, 3], request_id=42)
    add(remove_entity, "max_values", **maxed)
    for i in range(3):
        add(remove_entity, f"random_{i}", **rand_sim())
    return out


# ---------------------------------------------------------------------------------------------------------------
# Warfare

def warfare(p, c):
    """Fields shared by Fire and Detonation; returns the expected Godot fields."""
    p.firingEntityID = eid(c.get("firing_entity_id", [0, 0, 0]))
    p.targetEntityID = eid(c.get("target_entity_id", [0, 0, 0]))
    p.eventID = evid(c.get("event_id", [0, 0, 0]))
    location = [float(v) for v in c.get("location_ecef", [0.0, 0.0, 0.0])]
    p.location = WorldCoordinates(*location)
    velocity = [f32(v) for v in c.get("velocity", [0.0, 0.0, 0.0])]
    p.velocity = Vector3Float(*velocity)
    munition = c.get("munition_type", [0] * 7)
    p.descriptor = MunitionDescriptor(EntityType(*munition), c.get("warhead", 0), c.get("fuse", 0),
                                      c.get("quantity", 0), c.get("rate", 0))
    return header_fields(c) | {
        "firing_entity_id": c.get("firing_entity_id", [0, 0, 0]),
        "target_entity_id": c.get("target_entity_id", [0, 0, 0]),
        "munition_id": c.get("munition_id", [0, 0, 0]), "event_id": c.get("event_id", [0, 0, 0]),
        "location_ecef": f64_list(location), "velocity": velocity, "munition_type": munition,
        "warhead": c.get("warhead", 0), "fuse": c.get("fuse", 0), "quantity": c.get("quantity", 0),
        "rate": c.get("rate", 0)}


def fire(c):
    p = FirePdu()
    header(p, c)
    fields = warfare(p, c)
    p.munitionExpendableID = eid(c.get("munition_id", [0, 0, 0]))
    p.fireMissionIndex = c.get("fire_mission_index", 0)
    p.range = f32(c.get("range", 0.0))
    fields |= {"fire_mission_index": p.fireMissionIndex, "range": f64(p.range)}
    return "DisFirePdu", serialize(p, 96), fields


def detonation(c):
    p = DetonationPdu()
    header(p, c)
    fields = warfare(p, c)
    p.explodingEntityID = eid(c.get("munition_id", [0, 0, 0]))
    in_entity = [f32(v) for v in c.get("location_in_entity", [0.0, 0.0, 0.0])]
    p.locationInEntityCoordinates = Vector3Float(*in_entity)
    p.detonationResult = c.get("detonation_result", 0)
    p.pad = c.get("padding", 0)
    raw = c.get("variable_parameters", [])
    p.variableParameters = [VariableParameter(*struct.unpack(">BdIHB", r)) for r in raw]
    fields |= {"location_in_entity": in_entity, "detonation_result": p.detonationResult, "padding": p.pad,
               "variable_parameters": {"raw_hex": [r.hex() for r in raw]}}
    return "DisDetonationPdu", serialize(p, 104 + 16 * len(raw)), fields


def rand_warfare():
    return rand_header() | dict(
        firing_entity_id=rand_id(), target_entity_id=rand_id(), munition_id=rand_id(), event_id=rand_id(),
        location_ecef=[rng.uniform(-7e6, 7e6) for _ in range(3)],
        velocity=[rng.uniform(-1500, 1500) for _ in range(3)], munition_type=rand_type(),
        warhead=rng.getrandbits(16), fuse=rng.getrandbits(16), quantity=rng.getrandbits(16), rate=rng.getrandbits(16))


def warfare_cases():
    out = []
    add = lambda kind, name, **kw: out.append((kind, name, kw))  # noqa: E731
    tank_120mm = [2, 9, 225, 2, 14, 0, 0]  # munition, other domain (anti-armor), US, ballistic, 120 mm
    maxed = dict(exercise_id=255, timestamp=U32, pdu_status=255, firing_entity_id=[U16] * 3,
                 target_entity_id=[U16] * 3, munition_id=[U16] * 3, event_id=[U16] * 3,
                 munition_type=[255, 255, U16, 255, 255, 255, 255], warhead=U16, fuse=U16, quantity=U16, rate=U16)
    ankara = geodetic_to_ecef(39.93, 32.86, 900.0)
    target = geodetic_to_ecef(39.95, 32.88, 880.0)

    add(fire, "default")
    add(fire, "tank_round_ankara", firing_entity_id=[1, 1, 1], target_entity_id=[2, 1, 3], munition_id=[1, 1, 0],
        event_id=[1, 1, 17], location_ecef=ankara, velocity=[-950.5, 1200.25, 310.0], munition_type=tank_120mm,
        warhead=1000, fuse=1000, quantity=1, rate=0, range=2850.5, fire_mission_index=0)
    add(fire, "artillery_salvo", firing_entity_id=[2, 1, 4], munition_id=[2, 1, 900], event_id=[2, 1, 1],
        location_ecef=geodetic_to_ecef(39.99, 33.1, 1000.0), velocity=[100.0, -200.0, 600.0],
        munition_type=[2, 9, 0, 2, 0, 0, 0], warhead=1000, fuse=2000, quantity=6, rate=60, range=18000.0,
        fire_mission_index=12)
    add(fire, "max_values", fire_mission_index=U32, range=3.4e38, velocity=[3.4e38, -3.4e38, 1e-40],
        location_ecef=[-1.7976931348623157e308, 1e300, -4.9e-324], **maxed)
    for i in range(5):
        add(fire, f"random_{i}", fire_mission_index=rng.getrandbits(32), range=rng.uniform(0, 4e4),
            **rand_warfare())

    add(detonation, "default")
    add(detonation, "entity_impact", firing_entity_id=[1, 1, 1], target_entity_id=[2, 1, 3],
        munition_id=[1, 1, 0], event_id=[1, 1, 17], location_ecef=target, velocity=[-900.0, 1100.5, -50.25],
        munition_type=tank_120mm, warhead=1000, fuse=1000, quantity=1, location_in_entity=[1.5, 0.25, -0.75],
        detonation_result=1)
    add(detonation, "ground_impact", firing_entity_id=[2, 1, 4], munition_id=[2, 1, 900], event_id=[2, 1, 1],
        location_ecef=geodetic_to_ecef(39.9, 32.7, 850.0), munition_type=[2, 9, 0, 2, 0, 0, 0], quantity=6,
        rate=60, detonation_result=3)
    add(detonation, "one_variable_parameter", detonation_result=5,
        variable_parameters=[vp_bytes(0, 1.5, 0xDEADBEEF, 0xCAFE, 0x7F)])
    add(detonation, "three_variable_parameters", detonation_result=2,
        variable_parameters=[vp_bytes(0, 3.14159, 4096, 1, 2), vp_bytes(1, -2.5e10, 0x01020304, 0x0506, 0x07),
                             vp_bytes(4, 0.0, U32, U16, 0xFF)])
    add(detonation, "max_values", location_in_entity=[3.4e38, -3.4e38, -0.0], detonation_result=255, padding=U16,
        velocity=[1e-40, 0.0, -1e-40], location_ecef=[1.7976931348623157e308, -1e-300, 0.0], **maxed)
    for result in (0, 4, 6, 17, 24, 25):
        add(detonation, f"result_{result}", event_id=[3, 3, result], detonation_result=result)
    for i in range(5):
        add(detonation, f"random_{i}", location_in_entity=[rng.uniform(-20, 20) for _ in range(3)],
            detonation_result=rng.randrange(256),
            variable_parameters=[vp_bytes(rng.randrange(5), rng.uniform(-1e3, 1e3), rng.getrandbits(32),
                                          rng.getrandbits(16), rng.getrandbits(8))
                                 for _ in range(rng.randrange(0, 3))],
            **rand_warfare())
    return out


# ---------------------------------------------------------------------------------------------------------------

def self_check(stem, data, fields):
    """Decodes the bytes with opendis again and compares a few fields that are easy to read back."""
    pdu = createPdu(data)
    assert pdu is not None, stem
    assert pdu.exerciseID == fields["exercise_id"] and pdu.timestamp == fields["timestamp"], stem
    if "request_id" in fields:
        assert pdu.requestID == fields["request_id"], stem
        o = pdu.originatingEntityID
        assert [o.simulationAddress.site, o.simulationAddress.application, o.entityNumber] == \
            fields["originating_id"], stem
    if "event_id" in fields:
        e = pdu.eventID
        assert [e.simulationAddress.site, e.simulationAddress.application, e.eventNumber] == fields["event_id"], stem
        assert [pdu.descriptor.quantity, pdu.descriptor.rate] == [fields["quantity"], fields["rate"]], stem


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for f in OUT.glob("*.bin"):
        f.unlink()
    for f in OUT.glob("*.json"):
        f.unlink()
    names = []
    counters = {}
    for kind, name, c in sim_cases() + warfare_cases():
        cls, data, fields = kind(c)
        prefix = kind.__name__
        n = counters.get(prefix, 0)
        counters[prefix] = n + 1
        stem = f"{prefix}_{n:02d}_{name}"
        self_check(stem, data, fields)
        (OUT / f"{stem}.bin").write_bytes(data)
        (OUT / f"{stem}.json").write_text(json.dumps({"class": cls, "length": len(data), "fields": fields},
                                                     indent=2) + "\n", newline="\n")
        names.append(stem)
    (OUT / "index.json").write_text(json.dumps(names, indent=2) + "\n", newline="\n")
    print(f"wrote {len(names)} cases to {OUT}: {counters}")


main()
