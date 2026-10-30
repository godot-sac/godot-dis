"""Sends Entity State PDUs built with Open-DIS Python (the oracle) over UDP.

Usage: tools/.venv/Scripts/python tools/oracle_sender.py --port 3001 [--host 127.0.0.1] [--count 5]
       [--exercise 1] [--site 7] [--application 8] [--interval 0.05] [--repeat 3] [--broadcast]

Entity n (1..count) gets marking "PY<n>" and location (n * 1000, 2 * n * 1000, 3 * n * 1000) so a
receiver can check every field it decodes.
"""
import argparse
import io
import socket
import time

from opendis.stream import DataOutputStream
from opendis.dis7 import EntityStatePdu


def build(args, n):
    pdu = EntityStatePdu()
    pdu.exerciseID = args.exercise
    pdu.entityID.simulationAddress.site = args.site
    pdu.entityID.simulationAddress.application = args.application
    pdu.entityID.entityNumber = n
    pdu.forceId = 1 + n % 3
    pdu.entityType.entityKind = 1
    pdu.entityType.domain = 1
    pdu.entityType.country = 225
    pdu.entityLocation.x = n * 1000.0
    pdu.entityLocation.y = 2 * n * 1000.0
    pdu.entityLocation.z = 3 * n * 1000.0
    pdu.entityLinearVelocity.x = float(n)
    pdu.marking.characterSet = 1
    pdu.marking.characters = [ord(c) for c in f"PY{n}"] + [0] * (11 - len(f"PY{n}"))
    pdu.length = 144  # opendis does not fill the header length itself
    buf = io.BytesIO()
    pdu.serialize(DataOutputStream(buf))
    return buf.getvalue()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=3000)
    parser.add_argument("--count", type=int, default=5)
    parser.add_argument("--exercise", type=int, default=1)
    parser.add_argument("--site", type=int, default=7)
    parser.add_argument("--application", type=int, default=8)
    parser.add_argument("--interval", type=float, default=0.05)
    parser.add_argument("--repeat", type=int, default=3, help="send the whole set this many times")
    parser.add_argument("--broadcast", action="store_true")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if args.broadcast:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    for _ in range(args.repeat):
        for n in range(1, args.count + 1):
            sock.sendto(build(args, n), (args.host, args.port))
            time.sleep(args.interval)


if __name__ == "__main__":
    main()
