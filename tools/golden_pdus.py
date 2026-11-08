"""Generates golden PDUs of every type except Entity State (see golden.py) with the Open-DIS Python oracle.

Receiver and IFF are packed here with struct from the IEEE 1278.1-2012 layouts: Open-DIS Python 1.1.0 has no IFF PDU
and its Receiver PDU leaves out the radio reference ID and radio number. tests/unit/test_captures.gd checks both
against the opendis7-java capture and tshark.

Usage: tools/.venv/Scripts/python tools/golden_pdus.py
Writes tests/golden/pdus/<type>_<NN>_<name>.{bin,json} and tests/golden/pdus/index.json.

Each JSON names the Godot class and its expected property values, so one generic GUT test
(tests/unit/test_pdus_golden.gd) checks decode (bytes -> fields) and encode (fields -> identical bytes):
  {"class": "DisFirePdu", "length": 96, "fields": {"exercise_id": 1, "event_id": [1, 2, 3], ...}}
Values: ints and int lists as they are; float vectors as lists of float32 values; doubles as
{"f64": "<16 hex digits, big-endian IEEE bits>"} or {"f64_list": [...]} because Godot's JSON parser can be off in
the last bit; raw 16-byte variable parameters as {"raw_hex": [...]}; byte strings as {"bytes_hex": "..."};
datum records as {"fixed": [[id, value], ...]} and {"variable": [[id, length in bits, "hex"], ...]}.
"""
import io
import json
import math
import random
import struct
from pathlib import Path

from opendis.dis7 import (AcknowledgePdu, BeamStatus, ClockTime, CollisionPdu, CommentPdu, CreateEntityPdu, DataPdu,
                          DesignatorPdu, DetonationPdu, EEFundamentalParameterData, ElectromagneticEmissionsPdu,
                          EmissionSystemBeamRecord, EmissionSystemRecord, EmitterSystem, EntityIdentifier,
                          EntityStateUpdatePdu, EntityType, EulerAngles, FirePdu, FixedDatum, MunitionDescriptor,
                          RemoveEntityPdu, SetDataPdu, SignalPdu, SimulationAddress, StartResumePdu, StopFreezePdu,
                          TrackJamData, TransmitterPdu, VariableDatum, VariableParameter, Vector3Float,
                          WorldCoordinates)
from opendis.record.radio import ModulationType, SpreadSpectrum
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
# Collision, Create Entity, datum PDUs (Comment, Set Data, Data), radio PDUs (Transmitter, Signal).
# A separate random generator keeps the cases above unchanged.

rng2 = random.Random(1278 + 3)


def collision(c):
    p = CollisionPdu()
    header(p, c)
    p.issuingEntityID = eid(c.get("issuing_entity_id", [0, 0, 0]))
    p.collidingEntityID = eid(c.get("colliding_entity_id", [0, 0, 0]))
    p.eventID = evid(c.get("event_id", [0, 0, 0]))
    p.collisionType = c.get("collision_type", 0)
    velocity = [f32(v) for v in c.get("velocity", [0.0, 0.0, 0.0])]
    location = [f32(v) for v in c.get("location", [0.0, 0.0, 0.0])]
    p.velocity = Vector3Float(*velocity)
    p.mass = f32(c.get("mass", 0.0))
    p.location = Vector3Float(*location)
    fields = header_fields(c) | {
        "issuing_entity_id": c.get("issuing_entity_id", [0, 0, 0]),
        "colliding_entity_id": c.get("colliding_entity_id", [0, 0, 0]), "event_id": c.get("event_id", [0, 0, 0]),
        "collision_type": p.collisionType, "padding": 0, "velocity": velocity, "mass": f64(p.mass),
        "location": location}
    return "DisCollisionPdu", serialize(p, 60), fields


def create_entity(c):
    p = CreateEntityPdu()
    header(p, c)
    fields = header_fields(c) | sim_ids(p, c)
    p.requestID = c.get("request_id", 0)
    return "DisCreateEntityPdu", serialize(p, 28), fields


def datums(c):
    fixed = [FixedDatum(i, v) for i, v in c.get("fixed", [])]
    variable = [VariableDatum(i, len(data) * 8, list(data)) for i, data in c.get("variable", [])]
    expected = {"fixed_datums": {"fixed": [[i, v] for i, v in c.get("fixed", [])]},
                "variable_datums": {"variable": [[i, len(d) * 8, d.hex()] for i, d in c.get("variable", [])]}}
    length = 8 + 8 * len(fixed) + sum(8 + (len(d) + 7) // 8 * 8 for _, d in c.get("variable", []))
    return fixed, variable, expected, length


def comment(c):
    fixed, variable, expected, length = datums(c)
    p = CommentPdu(fixed, variable)
    header(p, c)
    fields = header_fields(c) | sim_ids(p, c) | expected
    fields.pop("request_id")
    return "DisCommentPdu", serialize(p, 24 + length), fields


def data_pdu(cls, name):
    def build(c):
        fixed, variable, expected, length = datums(c)
        p = cls(c.get("request_id", 0), fixed, variable)
        header(p, c)
        fields = header_fields(c) | sim_ids(p, c) | expected | {"padding": 0}
        return name, serialize(p, 32 + length), fields
    build.__name__ = "set_data" if name == "DisSetDataPdu" else "data"
    return build


set_data = data_pdu(SetDataPdu, "DisSetDataPdu")
data = data_pdu(DataPdu, "DisDataPdu")


def transmitter(c):
    kind, domain, country, category, version, nomenclature = c.get("radio_type", [0] * 6)
    p = TransmitterPdu(
        radioReferenceID=eid(c.get("radio_reference_id", [0, 0, 0])), radioNumber=c.get("radio_number", 0),
        radioEntityType=EntityType(kind, domain, country, category, version, nomenclature >> 8, nomenclature & 0xFF),
        transmitState=c.get("transmit_state", 0), inputSource=c.get("input_source", 0),
        antennaLocation=WorldCoordinates(*c.get("antenna_location_ecef", [0.0, 0.0, 0.0])),
        relativeAntennaLocation=Vector3Float(*[f32(v) for v in c.get("relative_antenna_location", [0.0] * 3)]),
        antennaPatternType=c.get("antenna_pattern_type", 0), frequency=c.get("frequency", 0),
        transmitFrequencyBandwidth=f32(c.get("bandwidth", 0.0)), power=f32(c.get("power", 0.0)),
        modulationType=ModulationType(SpreadSpectrum(*c.get("spread", [False, False, False])),
                                      *c.get("modulation", [0, 0, 0])),
        cryptoSystem=c.get("crypto_system", 0), cryptoKeyId=c.get("crypto_key_id", 0))
    header(p, c)
    data = serialize(p, 104)
    spread = struct.unpack(">H", data[88:90])[0]  # whatever bit order opendis uses for the spread spectrum field
    fields = header_fields(c) | {
        "radio_reference_id": c.get("radio_reference_id", [0, 0, 0]), "radio_number": c.get("radio_number", 0),
        "radio_type": [kind, domain, country, category, version, nomenclature],
        "transmit_state": c.get("transmit_state", 0), "input_source": c.get("input_source", 0),
        "antenna_location_ecef": f64_list(c.get("antenna_location_ecef", [0.0, 0.0, 0.0])),
        "relative_antenna_location": [f32(v) for v in c.get("relative_antenna_location", [0.0] * 3)],
        "antenna_pattern_type": c.get("antenna_pattern_type", 0), "frequency": c.get("frequency", 0),
        "bandwidth": f64(f32(c.get("bandwidth", 0.0))), "power": f64(f32(c.get("power", 0.0))),
        "modulation_type": [spread] + list(c.get("modulation", [0, 0, 0])),
        "crypto_system": c.get("crypto_system", 0), "crypto_key_id": c.get("crypto_key_id", 0)}
    return "DisTransmitterPdu", data, fields


def signal(c):
    payload = c.get("data", b"")
    assert len(payload) % 4 == 0  # opendis does not pad the data to 32 bits; keep cases where no padding is needed
    p = SignalPdu(entityID=eid(c.get("radio_reference_id", [0, 0, 0])), radioID=c.get("radio_number", 0),
                  encodingScheme=c.get("encoding_scheme", 0), tdlType=c.get("tdl_type", 0),
                  sampleRate=c.get("sample_rate", 0), samples=c.get("samples", 0), data=list(payload))
    header(p, c)
    fields = header_fields(c) | {
        "radio_reference_id": c.get("radio_reference_id", [0, 0, 0]), "radio_number": c.get("radio_number", 0),
        "encoding_scheme": c.get("encoding_scheme", 0), "tdl_type": c.get("tdl_type", 0),
        "sample_rate": c.get("sample_rate", 0), "data_length": len(payload) * 8, "samples": c.get("samples", 0),
        "data": {"bytes_hex": payload.hex()}}
    return "DisSignalPdu", serialize(p, 32 + len(payload)), fields


def rand_bytes(n):
    return bytes(rng2.randrange(256) for _ in range(n))


# ---------------------------------------------------------------------------------------------------------------
# Entity State Update, Receiver, Distributed Emission Regeneration

def entity_state_update(c):
    vps = c.get("variable_parameters", [])
    p = EntityStateUpdatePdu(
        entityID=eid(c.get("entity_id", [0, 0, 0])),
        entityLinearVelocity=Vector3Float(*[f32(v) for v in c.get("linear_velocity", [0.0] * 3)]),
        entityLocation=WorldCoordinates(*c.get("location_ecef", [0.0] * 3)),
        entityOrientation=EulerAngles(*[f32(v) for v in c.get("orientation", [0.0] * 3)]),
        entityAppearance=c.get("appearance", 0))
    for raw in vps:
        vp = VariableParameter()
        vp.parse(__import__("opendis.stream", fromlist=["DataInputStream"]).DataInputStream(io.BytesIO(raw)))
        p.variableParameters.append(vp)
    header(p, c)
    data = serialize(p, 72 + 16 * len(vps))
    fields = header_fields(c) | {
        "entity_id": c.get("entity_id", [0, 0, 0]),
        "linear_velocity": [f32(v) for v in c.get("linear_velocity", [0.0] * 3)],
        "location_ecef": f64_list(c.get("location_ecef", [0.0] * 3)),
        "orientation": [f32(v) for v in c.get("orientation", [0.0] * 3)],
        "appearance": c.get("appearance", 0), "variable_parameters": {"raw_hex": [v.hex() for v in vps]}}
    return "DisEntityStateUpdatePdu", data, fields


def receiver(c):
    """IEEE 1278.1-2012 7.7.4, packed here (see the module docstring)."""
    data = struct.pack(">BBBBIHBB", c.get("protocol_version", 7), c.get("exercise_id", 1), 27, 4,
                       c.get("timestamp", 0), 36, c.get("pdu_status", 0), 0)
    rid, tid = c.get("radio_reference_id", [0, 0, 0]), c.get("transmitter_entity_id", [0, 0, 0])
    data += struct.pack(">HHHHHHfHHHH", *rid, c.get("radio_number", 0), c.get("receiver_state", 0), 0,
                        c.get("received_power", 0.0), *tid, c.get("transmitter_radio_number", 0))
    fields = header_fields(c) | {
        "radio_reference_id": rid, "radio_number": c.get("radio_number", 0),
        "receiver_state": c.get("receiver_state", 0), "received_power": f64(f32(c.get("received_power", 0.0))),
        "transmitter_entity_id": tid, "transmitter_radio_number": c.get("transmitter_radio_number", 0)}
    return "DisReceiverPdu", data, fields


BEAM_FLOATS = ["frequency", "frequency_range", "effective_radiated_power", "pulse_repetition_frequency",
               "pulse_width", "azimuth_center", "azimuth_sweep", "elevation_center", "elevation_sweep", "sweep_sync"]


def electromagnetic_emission(c):
    systems, json_systems, length = [], [], 28
    for sc in c.get("systems", []):
        beams, json_beams, system_bytes = [], [], 20
        for bc in sc.get("beams", []):
            targets = bc.get("track_jam", [])
            floats = [f32(bc.get(k, 0.0)) for k in BEAM_FLOATS]
            beam_bytes = 52 + 8 * len(targets)
            beams.append(EmissionSystemBeamRecord(
                beamDataLength=beam_bytes // 4, beamIDNumber=bc.get("beam_id", 0),
                beamParameterIndex=bc.get("parameter_index", 0),
                fundamentalParameterData=EEFundamentalParameterData(*floats),
                beamFunction=bc.get("function", 0), highDensityTrackJam=bc.get("high_density_track_jam", 0),
                beamStatus=BeamStatus(bc.get("status", 0)), jammingModeSequence=bc.get("jamming_technique", 0),
                trackJamRecords=[TrackJamData(eid(t[0]), t[1], t[2]) for t in targets]))
            json_beams.append({"beam_id": bc.get("beam_id", 0), "parameter_index": bc.get("parameter_index", 0),
                               "floats": [struct.pack(">d", v).hex() for v in floats],
                               "function": bc.get("function", 0),
                               "high_density_track_jam": bc.get("high_density_track_jam", 0),
                               "status": bc.get("status", 0), "jamming_technique": bc.get("jamming_technique", 0),
                               "track_jam": targets})
            system_bytes += beam_bytes
        location = [f32(v) for v in sc.get("location", [0.0] * 3)]
        systems.append(EmissionSystemRecord(
            systemDataLength=system_bytes // 4 if system_bytes // 4 <= 255 else 0,
            emitterSystem=EmitterSystem(sc.get("emitter_name", 0), sc.get("emitter_function", 0),
                                        sc.get("emitter_number", 0)),
            location=Vector3Float(*location), beamRecords=beams))
        json_systems.append({"emitter_name": sc.get("emitter_name", 0),
                             "emitter_function": sc.get("emitter_function", 0),
                             "emitter_number": sc.get("emitter_number", 0), "location": location,
                             "beams": json_beams})
        length += system_bytes
    p = ElectromagneticEmissionsPdu(emittingEntityID=eid(c.get("emitting_entity_id", [0, 0, 0])),
                                    eventID=evid(c.get("event_id", [0, 0, 0])),
                                    stateUpdateIndicator=c.get("state_update_indicator", 0), systems=systems)
    header(p, c)
    fields = header_fields(c) | {
        "emitting_entity_id": c.get("emitting_entity_id", [0, 0, 0]), "event_id": c.get("event_id", [0, 0, 0]),
        "state_update_indicator": c.get("state_update_indicator", 0), "systems": {"ee_systems": json_systems}}
    return "DisElectromagneticEmissionPdu", serialize(p, length), fields


def designator(c):
    p = DesignatorPdu(
        designatingEntityID=eid(c.get("designating_entity_id", [0, 0, 0])), codeName=c.get("code_name", 0),
        designatedEntityID=eid(c.get("designated_entity_id", [0, 0, 0])),
        designatorCode=c.get("designator_code", 0), designatorPower=f32(c.get("power", 0.0)),
        designatorWavelength=f32(c.get("wavelength", 0.0)),
        designatorSpotWrtDesignated=Vector3Float(*[f32(v) for v in c.get("spot_offset", [0.0] * 3)]),
        designatorSpotLocation=WorldCoordinates(*c.get("spot_location_ecef", [0.0] * 3)),
        # opendis writes this byte as signed
        deadReckoningAlgorithm=c.get("dr_algorithm", 0) - (256 if c.get("dr_algorithm", 0) >= 128 else 0),
        entityLinearAcceleration=Vector3Float(*[f32(v) for v in c.get("spot_linear_acceleration", [0.0] * 3)]))
    header(p, c)
    fields = header_fields(c) | {
        "designating_entity_id": c.get("designating_entity_id", [0, 0, 0]), "code_name": c.get("code_name", 0),
        "designated_entity_id": c.get("designated_entity_id", [0, 0, 0]),
        "designator_code": c.get("designator_code", 0), "power": f64(f32(c.get("power", 0.0))),
        "wavelength": f64(f32(c.get("wavelength", 0.0))),
        "spot_offset": [f32(v) for v in c.get("spot_offset", [0.0] * 3)],
        "spot_location_ecef": f64_list(c.get("spot_location_ecef", [0.0] * 3)),
        "dr_algorithm": c.get("dr_algorithm", 0),
        "spot_linear_acceleration": [f32(v) for v in c.get("spot_linear_acceleration", [0.0] * 3)]}
    return "DisDesignatorPdu", serialize(p, 88), fields


IFF_INTS = ["system_type", "system_name", "system_mode", "change_options", "system_designator",
            "system_specific_data", "system_status", "data_field_1", "information_layers", "data_field_2"]


def iff(c):
    """IEEE 1278.1-2012 7.6.5, layer 1 plus raw further layers, packed here (see the module docstring)."""
    layers = c.get("layers", [])
    length = 60 + sum(len(x) for x in layers)
    data = struct.pack(">BBBBIHBB", c.get("protocol_version", 7), c.get("exercise_id", 1), 28, 6,
                       c.get("timestamp", 0), length, c.get("pdu_status", 0), 0)
    rel = [f32(v) for v in c.get("relative_antenna_location", [0.0] * 3)]
    v = {k: c.get(k, 0) for k in IFF_INTS}
    params = c.get("parameters", [0] * 6)
    data += struct.pack(">HHHHHHfffHHBBBBBBBB6H", *c.get("emitting_entity_id", [0, 0, 0]),
                        *c.get("event_id", [0, 0, 0]), *rel, *[v[k] for k in IFF_INTS], *params)
    data += b"".join(layers)
    assert len(data) == length
    fields = header_fields(c) | {
        "emitting_entity_id": c.get("emitting_entity_id", [0, 0, 0]), "event_id": c.get("event_id", [0, 0, 0]),
        "relative_antenna_location": rel, "parameters": params, "layers": {"raw_hex": [x.hex() for x in layers]}} | v
    return "DisIffPdu", data, fields


def iff_layer(number, info, payload):
    return struct.pack(">BBH", number, info, 4 + len(payload)) + payload


rng3 = random.Random(1278 + 4)


def emission_cases():
    out = []
    add = lambda kind, name, **kw: out.append((kind, name, kw))  # noqa: E731
    ids = lambda: [rng3.getrandbits(16) for _ in range(3)]  # noqa: E731
    head = lambda: dict(exercise_id=rng3.randrange(256), timestamp=rng3.getrandbits(32),  # noqa: E731
                        pdu_status=rng3.randrange(256))
    vec = lambda r: [rng3.uniform(-r, r) for _ in range(3)]  # noqa: E731

    add(entity_state_update, "default")
    add(entity_state_update, "moving_tank", entity_id=[1, 1, 7], linear_velocity=[3.5, -1.25, 0.0],
        location_ecef=geodetic_to_ecef(41.02, 29.05, 40.0), orientation=[1.2, -0.05, 0.01], appearance=0x00400000)
    add(entity_state_update, "with_articulated_parts", entity_id=[2, 1, 3],
        location_ecef=geodetic_to_ecef(39.93, 32.86, 900.0), orientation=[0.3, 0.0, 0.0],
        variable_parameters=[vp_bytes(0, 0.75, 4096 + 11, 0, 0), vp_bytes(1, 1.5, 3, 7, 0)])
    for i in range(2):
        add(entity_state_update, f"random_{i}", entity_id=ids(), linear_velocity=vec(300),
            location_ecef=vec(7e6), orientation=vec(3.1), appearance=rng3.getrandbits(32),
            variable_parameters=[vp_bytes(rng3.randrange(256), rng3.uniform(-1e3, 1e3), rng3.getrandbits(32),
                                          rng3.getrandbits(16), rng3.randrange(256))
                                 for _ in range(rng3.randrange(3))], **head())

    add(receiver, "default")
    add(receiver, "receiving_vhf", radio_reference_id=[1, 1, 4], radio_number=1, receiver_state=2,
        received_power=-72.5, transmitter_entity_id=[2, 1, 6], transmitter_radio_number=2)
    for i in range(2):
        add(receiver, f"random_{i}", radio_reference_id=ids(), radio_number=rng3.getrandbits(16),
            receiver_state=rng3.getrandbits(16), received_power=rng3.uniform(-150, 50), transmitter_entity_id=ids(),
            transmitter_radio_number=rng3.getrandbits(16), **head())

    add(electromagnetic_emission, "no_systems")
    add(electromagnetic_emission, "search_radar", emitting_entity_id=[1, 1, 20], event_id=[1, 1, 5],
        state_update_indicator=1,
        systems=[{"emitter_name": 1000, "emitter_function": 2, "emitter_number": 1, "location": [0.0, 0.0, -6.5],
                  "beams": [{"beam_id": 1, "parameter_index": 3, "frequency": 3.0e9, "frequency_range": 1.0e8,
                             "effective_radiated_power": 80.0, "pulse_repetition_frequency": 1000.0,
                             "pulse_width": 1.5, "azimuth_center": 0.0, "azimuth_sweep": 3.14159,
                             "elevation_center": 0.1, "elevation_sweep": 0.35, "sweep_sync": 0.25,
                             "function": 2, "status": 0, "track_jam": [[[2, 1, 6], 0, 0]]}]}])
    add(electromagnetic_emission, "radar_and_jammer", emitting_entity_id=[2, 1, 9], event_id=[2, 1, 77],
        systems=[{"emitter_name": 2000, "emitter_function": 1, "emitter_number": 1,
                  "beams": [{"beam_id": 1, "frequency": 9.4e9, "function": 1,
                             "track_jam": [[[1, 1, 20], 1, 1], [[1, 1, 21], 1, 1]]},
                            {"beam_id": 2, "frequency": 9.5e9, "function": 4, "high_density_track_jam": 1}]},
                 {"emitter_name": 3000, "emitter_function": 11, "emitter_number": 2, "location": [1.0, 0.0, 0.0],
                  "beams": [{"beam_id": 1, "frequency": 1.2e9, "function": 3, "jamming_technique": 0x01020304,
                             "track_jam": [[[1, 1, 20], 1, 1]]}]}])
    for i in range(2):
        add(electromagnetic_emission, f"random_{i}", emitting_entity_id=ids(), event_id=ids(),
            state_update_indicator=rng3.randrange(256),
            systems=[{"emitter_name": rng3.getrandbits(16), "emitter_function": rng3.randrange(256),
                      "emitter_number": rng3.randrange(256), "location": vec(20),
                      "beams": [{**{k: rng3.uniform(0, 1e10) for k in BEAM_FLOATS},
                                 "beam_id": rng3.randrange(256), "parameter_index": rng3.getrandbits(16),
                                 "function": rng3.randrange(256), "high_density_track_jam": rng3.randrange(256),
                                 "status": rng3.randrange(2), "jamming_technique": rng3.getrandbits(32),
                                 "track_jam": [[ids(), rng3.randrange(256), rng3.randrange(256)]
                                               for _ in range(rng3.randrange(3))]}
                                for _ in range(rng3.randrange(1, 3))]}
                     for _ in range(rng3.randrange(1, 3))], **head())

    add(designator, "default")
    add(designator, "laser_on_tank", designating_entity_id=[1, 1, 30], code_name=5, designated_entity_id=[2, 1, 6],
        designator_code=1, power=10.0, wavelength=1.064, spot_offset=[0.5, 0.0, -1.0],
        spot_location_ecef=geodetic_to_ecef(41.03, 29.06, 15.0), dr_algorithm=2,
        spot_linear_acceleration=[0.0, 0.1, 0.0])
    for i in range(2):
        add(designator, f"random_{i}", designating_entity_id=ids(), code_name=rng3.getrandbits(16),
            designated_entity_id=ids(), designator_code=rng3.getrandbits(16), power=rng3.uniform(0, 1e4),
            wavelength=rng3.uniform(0, 20), spot_offset=vec(50), spot_location_ecef=vec(7e6),
            dr_algorithm=rng3.randrange(256), spot_linear_acceleration=vec(10), **head())

    add(iff, "default")
    add(iff, "transponder_modes_1_3_c", emitting_entity_id=[1, 1, 4], event_id=[1, 1, 12],
        relative_antenna_location=[0.0, 0.0, 0.8], system_type=1, system_name=2, system_mode=2, change_options=0,
        system_status=0x0f, parameters=[0x2034, 0, 0x2777, 0, 0x2190, 0])
    add(iff, "with_layer_2", emitting_entity_id=[2, 1, 6], event_id=[2, 1, 3], system_type=2, system_name=5,
        information_layers=0x06, layers=[iff_layer(2, 0, bytes(range(24)))])
    for i in range(2):
        add(iff, f"random_{i}", emitting_entity_id=ids(), event_id=ids(), relative_antenna_location=vec(10),
            parameters=[rng3.getrandbits(16) for _ in range(6)],
            layers=[iff_layer(rng3.randrange(2, 6), rng3.randrange(256), rand_bytes(4 * rng3.randrange(8)))
                    for _ in range(rng3.randrange(3))],
            **{k: rng3.getrandbits(16 if k in ("system_type", "system_name") else 8) for k in IFF_INTS}, **head())
    return out


def more_cases():
    out = []
    add = lambda kind, name, **kw: out.append((kind, name, kw))  # noqa: E731
    ids = lambda: [rng2.getrandbits(16) for _ in range(3)]  # noqa: E731
    head = lambda: dict(exercise_id=rng2.randrange(256), timestamp=rng2.getrandbits(32),  # noqa: E731
                        pdu_status=rng2.randrange(256))

    add(collision, "default")
    add(collision, "truck_hits_car", issuing_entity_id=[1, 1, 5], colliding_entity_id=[3, 1, 2], event_id=[1, 1, 9],
        collision_type=0, velocity=[12.5, -3.25, 0.5], mass=18000.0, location=[4.0, 0.5, -0.25])
    add(collision, "elastic_unknown_object", issuing_entity_id=[2, 1, 7], event_id=[2, 1, 1], collision_type=1,
        velocity=[-1.0, 2.0, -3.0], mass=1200.5)
    for i in range(3):
        add(collision, f"random_{i}", issuing_entity_id=ids(), colliding_entity_id=ids(), event_id=ids(),
            collision_type=rng2.randrange(256), velocity=[rng2.uniform(-300, 300) for _ in range(3)],
            mass=rng2.uniform(0, 1e5), location=[rng2.uniform(-20, 20) for _ in range(3)], **head())

    add(create_entity, "default")
    add(create_entity, "create_tank", originating_id=[1, 1, 0], receiving_id=[2, 1, 0], request_id=77)
    add(create_entity, "max_values", exercise_id=255, timestamp=U32, pdu_status=255, originating_id=[U16] * 3,
        receiving_id=[U16] * 3, request_id=U32)

    add(comment, "empty")
    add(comment, "text", originating_id=[1, 1, 0], receiving_id=[U16, U16, U16],
        variable=[(1, "Phase line ALPHA crossed".encode())])
    add(comment, "text_and_values", originating_id=[2, 1, 0], receiving_id=[1, 1, 0], fixed=[(240000, 42), (1, U32)],
        variable=[(2, "Türkçe ç ğ ı ö ş ü".encode()), (3, bytes(range(16))), (4, b"x")])
    for i in range(2):
        add(comment, f"random_{i}", originating_id=ids(), receiving_id=ids(),
            fixed=[(rng2.getrandbits(32), rng2.getrandbits(32)) for _ in range(rng2.randrange(3))],
            variable=[(rng2.getrandbits(32), rand_bytes(rng2.randrange(1, 30))) for _ in range(rng2.randrange(3))],
            **head())

    for kind in (set_data, data):
        add(kind, "empty")
        add(kind, "fuel_and_callsign", originating_id=[1, 1, 0], receiving_id=[2, 1, 4], request_id=12,
            fixed=[(57600, 1500), (57601, 3)], variable=[(31010, b"VIPER-1")])
        add(kind, "random_0", originating_id=ids(), receiving_id=ids(), request_id=rng2.getrandbits(32),
            fixed=[(rng2.getrandbits(32), rng2.getrandbits(32)) for _ in range(4)],
            variable=[(rng2.getrandbits(32), rand_bytes(rng2.randrange(1, 40))) for _ in range(2)], **head())

    add(transmitter, "default")
    add(transmitter, "vhf_on_air", radio_reference_id=[1, 1, 4], radio_number=1, radio_type=[7, 2, 225, 1, 1, 3],
        transmit_state=2, input_source=1, antenna_location_ecef=geodetic_to_ecef(39.93, 32.86, 1400.0),
        relative_antenna_location=[0.5, 0.0, -1.2], antenna_pattern_type=0, frequency=243_000_000,
        bandwidth=25000.0, power=40.0, modulation=[3, 2, 0], crypto_system=0)
    add(transmitter, "frequency_hopping", radio_reference_id=[2, 1, 6], radio_number=2,
        radio_type=[7, 1, 0, 3, 2, 0x1234], transmit_state=1, frequency=30_000_000_000, bandwidth=1.5e6,
        power=-3.5, spread=[True, False, False], modulation=[2, 1, 2], crypto_system=4, crypto_key_id=0x8001)
    for i in range(2):
        add(transmitter, f"random_{i}", radio_reference_id=ids(), radio_number=rng2.getrandbits(16),
            radio_type=[rng2.randrange(256), rng2.randrange(256), rng2.getrandbits(16), rng2.randrange(256),
                        rng2.randrange(256), rng2.getrandbits(16)],
            transmit_state=rng2.randrange(3), input_source=rng2.randrange(256),
            antenna_location_ecef=[rng2.uniform(-7e6, 7e6) for _ in range(3)],
            relative_antenna_location=[rng2.uniform(-10, 10) for _ in range(3)],
            antenna_pattern_type=rng2.getrandbits(16), frequency=rng2.getrandbits(40),
            bandwidth=rng2.uniform(0, 1e7), power=rng2.uniform(-50, 60),
            modulation=[rng2.getrandbits(16) for _ in range(3)], crypto_system=rng2.getrandbits(16),
            crypto_key_id=rng2.getrandbits(16), **head())

    add(signal, "default")
    add(signal, "voice_block", radio_reference_id=[1, 1, 4], radio_number=1, encoding_scheme=0x0004, tdl_type=0,
        sample_rate=8000, samples=320, data=rand_bytes(320))
    add(signal, "data_link", radio_reference_id=[2, 1, 6], radio_number=2, encoding_scheme=0x4000 | 12,
        tdl_type=9, sample_rate=0, samples=1, data=rand_bytes(36))
    for i in range(2):
        add(signal, f"random_{i}", radio_reference_id=ids(), radio_number=rng2.getrandbits(16),
            encoding_scheme=rng2.getrandbits(16), tdl_type=rng2.getrandbits(16), sample_rate=rng2.getrandbits(32),
            samples=rng2.getrandbits(16), data=rand_bytes(4 * rng2.randrange(0, 60)), **head())
    return out


# ---------------------------------------------------------------------------------------------------------------

def self_check(stem, data, fields):
    """Decodes the bytes with opendis again and compares a few fields that are easy to read back."""
    if stem.startswith("iff_"):
        return  # Open-DIS Python has no IFF PDU
    pdu = createPdu(data)
    assert pdu is not None, stem
    assert pdu.exerciseID == fields["exercise_id"] and pdu.timestamp == fields["timestamp"], stem
    if "request_id" in fields:
        assert pdu.requestID == fields["request_id"], stem
        o = pdu.originatingEntityID
        assert [o.simulationAddress.site, o.simulationAddress.application, o.entityNumber] == \
            fields["originating_id"], stem
    if "quantity" in fields:
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
    for kind, name, c in sim_cases() + warfare_cases() + more_cases() + emission_cases():
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
