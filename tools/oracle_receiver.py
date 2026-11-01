"""Receives DIS PDUs and decodes them with Open-DIS Python (the oracle), one JSON line per PDU.

Usage: tools/.venv/Scripts/python tools/oracle_receiver.py --port 3002 --out received.jsonl
       [--expect 5] [--timeout 10]

Writes "<out>.ready" once the socket is bound so a test can start sending, and stops after
`--expect` PDUs or `--timeout` seconds. The output is written at the end, followed by "<out>.done"
(Windows may not let another process read a file while it is still open for writing).
"""
import argparse
import io
import json
import pathlib
import socket
import time

from opendis.PduFactory import createPdu
from opendis.stream import DataOutputStream


def reencoded(pdu):
    """The PDU serialized again by Open-DIS Python: equal to the received bytes if every field was understood."""
    buf = io.BytesIO()
    pdu.serialize(DataOutputStream(buf))
    return buf.getvalue().hex()


def describe(pdu, data, sender):
    out = {"type": type(pdu).__name__, "length": len(data), "sender": f"{sender[0]}:{sender[1]}",
           "hex": data.hex(), "reencoded": reencoded(pdu)}
    if type(pdu).__name__ == "EntityStatePdu":
        e = pdu.entityID
        out.update(
            exercise_id=pdu.exerciseID,
            entity_id=[e.simulationAddress.site, e.simulationAddress.application, e.entityNumber],
            force_id=pdu.forceId,
            marking=bytes(c & 0xFF for c in pdu.marking.characters).rstrip(b"\0").decode("latin-1"),
            location_ecef=[pdu.entityLocation.x, pdu.entityLocation.y, pdu.entityLocation.z],
            orientation=[pdu.entityOrientation.psi, pdu.entityOrientation.theta, pdu.entityOrientation.phi],
            length_field=pdu.length,
        )
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=3000)
    parser.add_argument("--out", required=True)
    parser.add_argument("--expect", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=10.0)
    args = parser.parse_args()

    out = pathlib.Path(args.out)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", args.port))
    sock.settimeout(0.2)
    pathlib.Path(str(out) + ".ready").write_text("ready\n")

    lines = []
    deadline = time.monotonic() + args.timeout
    while len(lines) < args.expect and time.monotonic() < deadline:
        try:
            data, sender = sock.recvfrom(65536)
        except socket.timeout:
            continue
        try:
            lines.append(describe(createPdu(data), data, sender))
        except Exception as error:  # report undecodable packets instead of crashing
            lines.append({"error": repr(error), "length": len(data)})
    out.write_text("".join(json.dumps(line) + "\n" for line in lines), encoding="utf-8", newline="\n")
    pathlib.Path(str(out) + ".done").write_text("done\n")


if __name__ == "__main__":
    main()
