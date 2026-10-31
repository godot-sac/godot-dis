"""Neutral civilian traffic for the godot-dis demo, sent with Open-DIS Python (an independent DIS implementation).

Usage: tools/.venv/Scripts/python.exe tools/demo_traffic.py [--group 239.1.2.3] [--port 3000] [--seconds 0]

Sends Entity State PDUs (site 3, force Neutral) for an airliner and some road traffic around the demo origin
(Ankara) once per second with dead reckoning algorithm 2, so the Godot demos move them smoothly in between.
--seconds 0 runs until Ctrl+C.
"""
import argparse
import io
import math
import socket
import struct
import time

import pymap3d
from opendis.RangeCoordinates import GPS
from opendis.dis7 import EntityStatePdu
from opendis.stream import DataOutputStream

ORIGIN = (39.9334, 32.8597, 938.0)  # same as demo/main.tscn
SITE, APPLICATION, FORCE_NEUTRAL = 3, 1, 3

# marking, entity type, route in demo map metres (x east, z south), speed m/s, height above origin m
UNITS = [
    ("THY-1", (1, 2, 0, 87, 0, 0, 0), [(-38000, 26000), (38000, -26000)], 230.0, 10000.0),
    ("CAR-1", (1, 1, 0, 81, 0, 0, 0), [(-12000, 14000), (16000, 13000), (16000, 16000), (-12000, 17000)], 22.0, 0.0),
    ("CAR-2", (1, 1, 0, 81, 0, 0, 0), [(16000, 16000), (-12000, 17000), (-12000, 14000), (16000, 13000)], 18.0, 0.0),
    ("BUS-1", (1, 1, 0, 82, 0, 0, 0), [(-2000, 20000), (-2000, 9000)], 14.0, 0.0),
]


def route_point(route, distance):
    """Position (x, z) and heading (radians, clockwise from north) at `distance` along a closed route."""
    segments = [(route[i], route[(i + 1) % len(route)]) for i in range(len(route))]
    total = sum(math.dist(a, b) for a, b in segments)
    distance %= total
    for a, b in segments:
        length = math.dist(a, b)
        if distance <= length:
            t = distance / length
            x, z = a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t
            heading = math.atan2(b[0] - a[0], -(b[1] - a[1]))  # east = +x, north = -z
            return (x, z), heading
        distance -= length
    return segments[-1][1], 0.0


def to_ecef(x, z, height):
    return pymap3d.enu2ecef(x, -z, height, *ORIGIN)


def build_pdu(number, marking, entity_type, ecef, velocity, euler):
    pdu = EntityStatePdu()
    pdu.exerciseID = 1
    pdu.entityID.simulationAddress.site = SITE
    pdu.entityID.simulationAddress.application = APPLICATION
    pdu.entityID.entityNumber = number
    pdu.forceId = FORCE_NEUTRAL
    t = pdu.entityType
    t.entityKind, t.domain, t.country, t.category, t.subcategory, t.specific, t.extra = entity_type
    pdu.entityLocation.x, pdu.entityLocation.y, pdu.entityLocation.z = (float(v) for v in ecef)
    pdu.entityLinearVelocity.x, pdu.entityLinearVelocity.y, pdu.entityLinearVelocity.z = velocity
    pdu.entityOrientation.psi, pdu.entityOrientation.theta, pdu.entityOrientation.phi = euler
    pdu.deadReckoningParameters.deadReckoningAlgorithm = 2
    pdu.marking.characterSet = 1
    pdu.marking.characters = [ord(c) for c in marking] + [0] * (11 - len(marking))
    pdu.length = 144  # opendis does not fill the header length itself
    buffer = io.BytesIO()
    pdu.serialize(DataOutputStream(buffer))
    return buffer.getvalue()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--group", default="239.1.2.3")
    parser.add_argument("--port", type=int, default=3000)
    parser.add_argument("--seconds", type=float, default=0.0)
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, struct.pack("b", 1))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)
    gps = GPS()
    start = time.monotonic()
    sent = 0
    print(f"sending {len(UNITS)} neutral entities (site {SITE}) to {args.group}:{args.port}, Ctrl+C to stop")
    try:
        while args.seconds <= 0 or time.monotonic() - start < args.seconds:
            elapsed = time.monotonic() - start
            for number, (marking, entity_type, route, speed, height) in enumerate(UNITS, start=1):
                (x, z), heading = route_point(route, speed * elapsed)
                (x2, z2), _ = route_point(route, speed * (elapsed + 1.0))
                here, ahead = to_ecef(x, z, height), to_ecef(x2, z2, height)
                velocity = tuple(float(b - a) for a, b in zip(here, ahead))  # m/s over the next second
                lat, lon, alt = pymap3d.enu2geodetic(x, -z, height, *ORIGIN)
                _, _, _, psi, theta, phi = gps.llarpy2ecef(math.radians(lat), math.radians(lon), alt, 0.0, 0.0, heading)
                sock.sendto(build_pdu(number, marking, entity_type, here, velocity, (psi, theta, phi)),
                            (args.group, args.port))
                sent += 1
            time.sleep(1.0)
    except KeyboardInterrupt:
        pass
    print(f"sent {sent} PDUs")


if __name__ == "__main__":
    main()
