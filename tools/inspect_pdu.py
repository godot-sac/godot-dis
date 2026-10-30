"""Decodes a DIS PDU with the Open-DIS Python oracle and prints its fields.

Usage: tools/.venv/Scripts/python tools/inspect_pdu.py <file.bin>
"""
import sys

from opendis.PduFactory import createPdu


def fields(obj, indent=""):
    for name, value in sorted(vars(obj).items()):
        if hasattr(value, "__dict__"):
            print(f"{indent}{name}:")
            fields(value, indent + "  ")
        else:
            print(f"{indent}{name}: {value!r}")


data = open(sys.argv[1], "rb").read()
print(f"{len(data)} bytes: {data.hex()}")
pdu = createPdu(data)
print(type(pdu).__name__)
fields(pdu)
