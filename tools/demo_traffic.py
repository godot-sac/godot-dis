"""Neutral civilian traffic for the godot-dis demo, sent with Open-DIS Python (an independent DIS implementation).

Usage: tools/.venv/Scripts/python.exe tools/demo_traffic.py [--group 239.1.2.3] [--port 3000] [--seconds 0]
       [--start 0] [--rate 1]

Sends Entity State PDUs (site 3, force Neutral) for an airliner, some road traffic and a Bosphorus ferry (Üsküdar -
Beşiktaş) around the demo origin (Üsküdar, Istanbul) once per second with dead reckoning algorithm 4 (RVW:
velocity, acceleration and turn rate), so the Godot demos move them smoothly in between, also through turns.
--seconds 0 runs until Ctrl+C. --start skips ahead along the routes (seconds of scenario time; the airliner reaches
its first turn after ~310 s); --rate sends that many updates per second.

It is also a managed application: Stop/Freeze and Start/Resume addressed to site 3 (or everyone) freeze and resume
the traffic, a Remove Entity request removes that vehicle, and every request is answered with an Acknowledge PDU,
all built with Open-DIS Python. The airliner's radio (Transmitter PDU every 5 s) talks for 3 s every 20 s
(Signal PDUs with silent audio), so the demo map shows it on the air.
"""
import argparse
import io
import math
import socket
import struct
import time

import pymap3d
from opendis.PduFactory import createPdu
from opendis.RangeCoordinates import GPS
from opendis.dis7 import AcknowledgePdu, EntityStatePdu, EntityType, SignalPdu, TransmitterPdu, WorldCoordinates
from opendis.stream import DataOutputStream

ORIGIN = (41.02, 29.05, 0.0)  # Üsküdar, Istanbul; same as demo/main.tscn
SITE, APPLICATION, FORCE_NEUTRAL = 3, 1, 3
ALL = 0xFFFF
DEACTIVATED = 1 << 23  # general appearance "State" bit
# Request PDU class -> acknowledge flag (SISO-REF-010 UID 69)
ACKNOWLEDGE_FLAGS = {"RemoveEntityPdu": 2, "StartResumePdu": 3, "StopFreezePdu": 4}
ABLE_TO_COMPLY, UNABLE_TO_COMPLY = 1, 2
RADIO_ENTITY = 1  # THY-1
RADIO_FREQUENCY = 118_100_000  # Hz, an approach frequency
TALK_EVERY, TALK_FOR = 20.0, 3.0  # seconds


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


# Main roads of the Asian side (© OpenStreetMap contributors, ODbL; via the godot-geo terrain package), demo map
# metres: a car loop Altunizade - Acıbadem - Kadıköy (Hasanpaşa) - Çamlıca, a bus loop Üsküdar - Altunizade.
CAR_ROUTE = [
    (480, 580), (-60, 1380), (-270, 1510), (-530, 1860), (-780, 2060), (-1010, 1920), (-760, 1920), (110, 2170),
    (400, 2300), (450, 2460), (560, 2520), (640, 2500), (700, 2430), (750, 2380), (840, 2470), (1760, 2920),
    (1960, 2870), (2020, 2590), (1870, 1940), (1880, 1710), (2100, 930), (2130, 520), (1630, 40), (1250, -120),
    (1180, -90), (1090, 40), (820, 180),
]
BUS_ROUTE = [
    (-2890, -640), (-2830, -640), (-2700, -560), (-2640, -570), (-2320, -690), (-1990, -870), (-1790, -890),
    (-1480, -820), (-1310, -690), (-1130, -440), (-1140, -360), (-1100, -400), (-730, -390), (-500, -340),
    (-400, -280), (-890, -160), (-1220, -140), (-1280, -20), (-1820, 540), (-2030, 400), (-2370, 260),
    (-2710, -50), (-2860, -260),
]
SEA_SURFACE = 36.3  # m above the origin (ellipsoid): EGM2008 geoid height there less Earth curvature at ~3 km

# marking, entity type, route in demo map metres (x east, z south), speed m/s, height above origin m
UNITS = [
    ("THY-1", (1, 2, 0, 87, 0, 0, 0), racetrack((-20000, 12000), (20000, -12000), 4000), 150.0, 3000.0),
    ("CAR-1", (1, 1, 0, 81, 0, 0, 0), CAR_ROUTE, 16.0, 0.0),
    ("CAR-2", (1, 1, 0, 81, 0, 0, 0), CAR_ROUTE[::-1], 14.0, 0.0),
    ("BUS-1", (1, 1, 0, 82, 0, 0, 0), BUS_ROUTE, 10.0, 0.0),
    ("FERRY-1", (1, 3, 0, 80, 4, 0, 0), racetrack((-3150, -800), (-3450, -2150), 80, 4), 7.0, SEA_SURFACE),
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


def serialize(pdu, length):
    pdu.length = length  # opendis does not fill the header length itself
    buffer = io.BytesIO()
    pdu.serialize(DataOutputStream(buffer))
    return buffer.getvalue()


def build_pdu(number, elapsed, appearance=0):
    """Entity State PDU of unit `number` (1-based) at scenario time `elapsed`."""
    marking, entity_type, route, speed, height = UNITS[number - 1]
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
    _, _, _, psi, theta, phi = GPS().llarpy2ecef(math.radians(lat), math.radians(lon), alt, 0.0, 0.0, heading)

    pdu = EntityStatePdu()
    pdu.exerciseID = 1
    pdu.entityID.simulationAddress.site = SITE
    pdu.entityID.simulationAddress.application = APPLICATION
    pdu.entityID.entityNumber = number
    pdu.forceId = FORCE_NEUTRAL
    pdu.entityAppearance = appearance
    t = pdu.entityType
    t.entityKind, t.domain, t.country, t.category, t.subcategory, t.specific, t.extra = entity_type
    pdu.entityLocation.x, pdu.entityLocation.y, pdu.entityLocation.z = (float(v) for v in here)
    pdu.entityLinearVelocity.x, pdu.entityLinearVelocity.y, pdu.entityLinearVelocity.z = velocity
    pdu.entityOrientation.psi, pdu.entityOrientation.theta, pdu.entityOrientation.phi = psi, theta, phi
    dr = pdu.deadReckoningParameters
    dr.deadReckoningAlgorithm = 4  # RVW
    dr.entityLinearAcceleration.x, dr.entityLinearAcceleration.y, dr.entityLinearAcceleration.z = acceleration
    dr.entityAngularVelocity.x, dr.entityAngularVelocity.y, dr.entityAngularVelocity.z = 0.0, 0.0, yaw_rate
    pdu.marking.characterSet = 1
    pdu.marking.characters = [ord(c) for c in marking] + [0] * (11 - len(marking))
    return serialize(pdu, 144)


def transmitter(scenario, transmitting):
    """Transmitter PDU of the airliner's radio, at the airliner's position."""
    _, _, route, speed, height = UNITS[RADIO_ENTITY - 1]
    (x, z), _ = route_point(route, speed * scenario)
    pdu = TransmitterPdu(radioNumber=1, radioEntityType=EntityType(7, 2, 0, 1, 1, 0, 0),
                         transmitState=2 if transmitting else 1, inputSource=1,
                         antennaLocation=WorldCoordinates(*(float(v) for v in to_ecef(x, z, height))),
                         frequency=RADIO_FREQUENCY, transmitFrequencyBandwidth=25000.0, power=40.0)
    pdu.exerciseID = 1
    pdu.radioReferenceID.simulationAddress.site = SITE
    pdu.radioReferenceID.simulationAddress.application = APPLICATION
    pdu.radioReferenceID.entityNumber = RADIO_ENTITY
    return serialize(pdu, 104)


def signal_block():
    """50 ms of silence as 16-bit linear PCM at 8 kHz (800 bytes)."""
    pdu = SignalPdu(radioID=1, encodingScheme=4, sampleRate=8000, samples=400, data=[0] * 800)
    pdu.exerciseID = 1
    pdu.entityID.simulationAddress.site = SITE
    pdu.entityID.simulationAddress.application = APPLICATION
    pdu.entityID.entityNumber = RADIO_ENTITY
    return serialize(pdu, 32 + 800)


def acknowledge(request, response):
    ack = AcknowledgePdu(acknowledgeFlag=ACKNOWLEDGE_FLAGS[type(request).__name__], responseFlag=response,
                         requestID=request.requestID)
    ack.exerciseID = 1
    ack.originatingEntityID.simulationAddress.site = SITE
    ack.originatingEntityID.simulationAddress.application = APPLICATION
    ack.receivingEntityID = request.originatingEntityID
    return serialize(ack, 32)


class Control:
    """The managed-application side of simulation management: frozen state, removed vehicles, answered requests."""

    def __init__(self):
        self.frozen = False
        self.removed = set()  # entity numbers
        self.handled = set()  # (site, application, PDU class, request ID) already carried out

    def handle(self, data):
        """Returns (Acknowledge bytes or None, entity number to deactivate or None)."""
        try:
            pdu = createPdu(data)
        except Exception:
            return None, None
        kind = type(pdu).__name__
        if kind not in ACKNOWLEDGE_FLAGS or pdu.exerciseID != 1:
            return None, None
        to = pdu.receivingEntityID
        if to.simulationAddress.site not in (SITE, ALL) or to.simulationAddress.application not in (APPLICATION, ALL):
            return None, None
        frm = pdu.originatingEntityID.simulationAddress
        key = (frm.site, frm.application, kind, pdu.requestID)
        repeated = key in self.handled  # the manager did not hear our answer and asks again
        self.handled.add(key)
        deactivate = None
        response = ABLE_TO_COMPLY
        if kind == "StopFreezePdu" and not repeated:
            self.frozen = True
            print(f"stop/freeze from {frm.site}:{frm.application} (reason {pdu.reason})")
        elif kind == "StartResumePdu" and not repeated:
            self.frozen = False
            print(f"start/resume from {frm.site}:{frm.application}")
        elif kind == "RemoveEntityPdu":
            number = to.entityNumber
            if repeated:
                pass
            elif 1 <= number <= len(UNITS) and number not in self.removed:
                self.removed.add(number)
                deactivate = number
                print(f"remove {UNITS[number - 1][0]} on request of {frm.site}:{frm.application}")
            else:
                response = UNABLE_TO_COMPLY
        return acknowledge(pdu, response), deactivate


def listener(group, port):
    """A second socket on the exercise port for simulation management PDUs (shared with other DIS programs)."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", port))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, socket.inet_aton(group) + socket.inet_aton("0.0.0.0"))
    sock.setblocking(False)
    return sock


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
    receiver = listener(args.group, args.port)
    control = Control()
    destination = (args.group, args.port)
    scenario = args.start  # scenario seconds; they stand still while frozen
    started = last = next_send = next_radio = time.monotonic()
    talking = False
    sent = 0
    print(f"sending {len(UNITS)} neutral entities (site {SITE}) to {args.group}:{args.port}, Ctrl+C to stop")
    try:
        while args.seconds <= 0 or time.monotonic() - started < args.seconds:
            while True:
                try:
                    data, _ = receiver.recvfrom(65536)
                except OSError:  # nothing waiting (BlockingIOError) or a socket hiccup
                    break
                ack, deactivate = control.handle(data)
                if ack:
                    sock.sendto(ack, destination)
                if deactivate:
                    sock.sendto(build_pdu(deactivate, scenario, appearance=DEACTIVATED), destination)
            now = time.monotonic()
            if not control.frozen:
                scenario += now - last
            last = now
            if now >= next_send:
                next_send = now + 1.0 / args.rate
                if not control.frozen:  # frozen: no updates ("transmit updates" is off in our Stop/Freeze)
                    for number in range(1, len(UNITS) + 1):
                        if number not in control.removed:
                            sock.sendto(build_pdu(number, scenario), destination)
                            sent += 1
            # The airliner's radio: talks for TALK_FOR seconds every TALK_EVERY seconds of scenario time.
            if RADIO_ENTITY not in control.removed and not control.frozen:
                talk = scenario % TALK_EVERY < TALK_FOR and scenario > 5.0
                if talk != talking or now >= next_radio:
                    talking = talk
                    next_radio = now + 5.0
                    sock.sendto(transmitter(scenario, talking), destination)
                if talking:
                    sock.sendto(signal_block(), destination)
            time.sleep(0.05)
    except KeyboardInterrupt:
        pass
    print(f"sent {sent} PDUs")


if __name__ == "__main__":
    main()
