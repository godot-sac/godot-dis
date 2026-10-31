"""Neutral civilian traffic for the godot-dis demo, sent with Open-DIS Python (an independent DIS implementation).

Usage: tools/.venv/Scripts/python.exe tools/demo_traffic.py [--group 239.1.2.3] [--port 3000] [--seconds 0]
       [--start 0] [--rate 1]

Sends Entity State PDUs (site 3, force Neutral) for an airliner and some road traffic around the demo origin
(Ankara) once per second with dead reckoning algorithm 4 (RVW: velocity, acceleration and turn rate), so the
Godot demos move them smoothly in between, also through turns.
--seconds 0 runs until Ctrl+C. --start skips ahead along the routes (seconds of scenario time; the airliner reaches
its first turn after ~400 s); --rate sends that many updates per second.
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



def racetrack(a, b, radius, points_per_turn=24):
    """Closed route: out along a -> b offset to one side, a half circle around b, back on the other side, a half
    circle around a. Back-and-forth routes made the vehicle reverse on the spot (speed drops to zero, the nose
    flips 180 degrees), which looked like jitter in the 3D demo."""
    ax, az = a
    bx, bz = b
    heading = math.atan2(bz - az, bx - ax)
    route = []
    for centre, start in ((b, heading - math.pi / 2), (a, heading + math.pi / 2)):
        for i in range(points_per_turn + 1):
            angle = start + math.pi * i / points_per_turn
            route.append((centre[0] + radius * math.cos(angle), centre[1] + radius * math.sin(angle)))
    return route[-1:] + route[:-1]  # start at a, heading for b


# marking, entity type, route in demo map metres (x east, z south), speed m/s, height above origin m
UNITS = [
    ("THY-1", (1, 2, 0, 87, 0, 0, 0), racetrack((-38000, 26000), (38000, -26000), 6000), 230.0, 10000.0),
    ("CAR-1", (1, 1, 0, 81, 0, 0, 0), [(-12000, 14000), (16000, 13000), (16000, 16000), (-12000, 17000)], 22.0, 0.0),
    ("CAR-2", (1, 1, 0, 81, 0, 0, 0), [(16000, 16000), (-12000, 17000), (-12000, 14000), (16000, 13000)], 18.0, 0.0),
    ("BUS-1", (1, 1, 0, 82, 0, 0, 0), racetrack((-2000, 20000), (-2000, 9000), 60, 8), 14.0, 0.0),
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


def build_pdu(number, marking, entity_type, ecef, velocity, acceleration, angular_velocity, euler):
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
    dr = pdu.deadReckoningParameters
    dr.deadReckoningAlgorithm = 4  # RVW
    dr.entityLinearAcceleration.x, dr.entityLinearAcceleration.y, dr.entityLinearAcceleration.z = acceleration
    dr.entityAngularVelocity.x, dr.entityAngularVelocity.y, dr.entityAngularVelocity.z = angular_velocity
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
    parser.add_argument("--start", type=float, default=0.0, help="scenario time at launch, seconds")
    parser.add_argument("--rate", type=float, default=1.0, help="updates per second")
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
            elapsed = time.monotonic() - start + args.start
            for number, (marking, entity_type, route, speed, height) in enumerate(UNITS, start=1):
                (x, z), heading = route_point(route, speed * elapsed)
                (xb, zb), _ = route_point(route, speed * (elapsed - 1.0))
                (xa, za), _ = route_point(route, speed * (elapsed + 1.0))
                here, behind, ahead = to_ecef(x, z, height), to_ecef(xb, zb, height), to_ecef(xa, za, height)
                # Central differences over +-1 s: velocity and acceleration in ECEF.
                velocity = tuple(float(a - b) / 2.0 for a, b in zip(ahead, behind))
                acceleration = tuple(float(a - 2.0 * h + b) for a, h, b in zip(ahead, here, behind))
                # Turn rate about the body z axis (down): positive when the heading grows (turning right).
                _, heading_ahead = route_point(route, speed * (elapsed + 0.5))
                _, heading_behind = route_point(route, speed * (elapsed - 0.5))
                yaw_rate = (heading_ahead - heading_behind + math.pi) % (2.0 * math.pi) - math.pi
                lat, lon, alt = pymap3d.enu2geodetic(x, -z, height, *ORIGIN)
                _, _, _, psi, theta, phi = gps.llarpy2ecef(math.radians(lat), math.radians(lon), alt, 0.0, 0.0, heading)
                sock.sendto(build_pdu(number, marking, entity_type, here, velocity, acceleration, (0.0, 0.0, yaw_rate),
                                      (psi, theta, phi)),
                            (args.group, args.port))
                sent += 1
            time.sleep(1.0 / args.rate)
    except KeyboardInterrupt:
        pass
    print(f"sent {sent} PDUs")


if __name__ == "__main__":
    main()
