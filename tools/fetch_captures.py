"""Downloads public DIS sample captures used as regression inputs (tests/unit/test_captures.gd).

Usage: tools/.venv/Scripts/python.exe tools/fetch_captures.py

Files go to tools/.cache/captures/ (not committed) and are checked against pinned SHA-256 hashes.
- Wireshark SampleCaptures (https://wiki.wireshark.org/SampleCaptures): no license is stated for them, so they are
  only downloaded for local testing, never redistributed with this project.
- NPS MOVES MV3500 course: AllPduSenderWiresharkCapture.pcapng, the PDUs opendis7-java's AllPduSender sends
  (BSD-style "Open-Source Software License for NPS MOVES Savage Research Group", see THIRD_PARTY_NOTICES.md).
"""
import hashlib
import pathlib
import sys
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
CACHE = ROOT / "tools" / ".cache" / "captures"

WIRESHARK = "https://wiki.wireshark.org/uploads/__moin_import__/attachments/SampleCaptures/"
NPS = ("https://gitlab.nps.edu/Savage/NetworkedGraphicsMV3500/-/raw/master/examples/src/OpenDis7Examples/")

# name -> (url, sha256)
FILES = {
    "DIS_EntityState_1.pcapng": (WIRESHARK + "DIS_EntityState_1.pcapng",
                                "74d671d65e34501b796e75a429d71a28d608b28ac00926f81c26552c0146ce3c"),
    "DIS_EntityState_2.pcapng": (WIRESHARK + "DIS_EntityState_2.pcapng",
                                "0f9d26dc8779785d78a664f05df78d988c8583aed98cf255b5b70035d2aa7810"),
    "DIS_EnvironmentalProcess.pcapng": (WIRESHARK + "DIS_EnvironmentalProcess.pcapng",
                                       "56a1bea302475f749dc5354e542442f97a766ce4609f165941ac91ec42b60955"),
    "DIS_Signal.pcapng": (WIRESHARK + "DIS_Signal.pcapng",
                         "d3fc32e53954b96cf7b71981aae0a5b0375eaf20c297fff7cc6a089add5216ec"),
    "DIS_signal_and_transmitter.pcapng": (WIRESHARK + "DIS_signal_and_transmitter.pcapng",
                                         "c2e9f0d9fc28b1c06df47d5b9e033b0399fa111a7d0fe3334b6134f713fa2ce2"),
    "AllPduSenderWiresharkCapture.pcapng": (NPS + "AllPduSenderWiresharkCapture.pcapng",
                                           "32fe4593fc7ad1e2754ff80a6d22596cd8cb59b2c9ba53cad5cb35ad4de32851"),
}


def main():
    CACHE.mkdir(parents=True, exist_ok=True)
    failed = False
    for name, (url, sha256) in FILES.items():
        target = CACHE / name
        if not target.exists():
            print(f"downloading {url}")
            request = urllib.request.Request(url, headers={"User-Agent": "godot-dis tools/fetch_captures.py"})
            with urllib.request.urlopen(request, timeout=60) as response:
                target.write_bytes(response.read())
        digest = hashlib.sha256(target.read_bytes()).hexdigest()
        if sha256 is None:
            print(f"{name}: {target.stat().st_size} bytes, sha256 {digest} (not pinned)")
        elif digest != sha256:
            print(f"{name}: SHA-256 mismatch ({digest}), deleting it", file=sys.stderr)
            target.unlink()
            failed = True
        else:
            print(f"{name}: ok ({target.stat().st_size} bytes)")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
