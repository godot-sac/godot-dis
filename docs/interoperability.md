# Interoperability

There is no DIS certification or plugfest today, so godot-dis is checked the way DIS implementations check each
other in practice: byte-for-byte against independent implementations, against real captures, and with Wireshark's
DIS dissector as a neutral decoder. Every check below is an automated test that runs with the GUT suite (checks
that need a tool or a download are *pending* when it is missing).

## Results (2026-11-01, godot-dis 0.1.0-dev, Windows 11 and Ubuntu 24.04)

| Against | Version | What | PDU types | Result | Test |
|---|---|---|---|---|---|
| Open-DIS Python | `opendis` 1.1.0 | Golden PDUs written by Open-DIS: our `encode()` gives identical bytes, our `decode()` gives every field | Entity State (31 cases), Fire (9), Detonation (17), Remove Entity (6), Start/Resume (8), Stop/Freeze (15), Acknowledge (30) | ✔ 116 / 116 | `test_entity_state_pdu.gd`, `test_pdus_golden.gd` |
| Open-DIS Python | 1.1.0 | Over UDP, Python → Godot: all golden PDUs arrive, decode and re-encode identically; Godot → Python: Open-DIS decodes each as the right class and serializes it back to the same bytes | same 7 types | ✔ | `test_network.gd` |
| opendis7-java | as captured on 2021-08-14 | NPS MV3500 `AllPduSenderWiresharkCapture.pcapng` (one PDU of each of the 72 DIS 7 types): every type we support decodes and re-encodes to the same bytes | Entity State, Fire, Detonation, Remove Entity, Start/Resume, Stop/Freeze, Acknowledge | ✔ 7 / 7 (see note 1) | `test_captures.gd` |
| Wireshark DIS dissector | tshark 4.6.9 | A pcapng written by `DisRecording` with all 116 golden PDUs: right PDU type and length for every frame, no malformed packets, no expert warnings; field values (marking, IDs, country, event number, range) as written | all 7 | ✔ | `test_wireshark.gd` |
| Wireshark SampleCaptures | wiki files, SHA-256 pinned | Entity State captures from other DIS senders decode and re-encode identically; Environmental Process, Signal and Transmitter captures are read and replayed (types we don't decode yet) | Entity State 289 PDUs | ✔ | `test_captures.gd` |
| Open-DIS Python application | demo traffic | Live exercise control: a Godot manager stops, resumes and removes entities of an Open-DIS Python application, which answers with Open-DIS Acknowledge PDUs; Godot ↔ Godot the same | Start/Resume, Stop/Freeze, Remove Entity, Acknowledge | ✔ (manual run, see below) | `docs/demo.md` |
| GRILL DIS for Unreal / Unity | — | not tested (no Unreal / Unity setup) | | — | |

Notes
1. The opendis7-java capture leaves the PDU **length field 0** in every PDU. godot-dis accepts a length of 0 as
   "the size of the datagram" when reading, and always writes the real length; the re-encoded bytes are compared
   with the length field filled in. A live test against current opendis7-java was not run (it needs JDK 21 and
   Ant; the distributed jar is about 300 MB).
2. Open-DIS Python writes the Stop/Freeze padding as a signed 16-bit number and the Clock Time hour as unsigned;
   both are the same bytes on the wire. It does not fill the length field either (the golden generator sets it).
3. Wireshark's dissector source (GPL) was not read; Wireshark is used only as a program.

## PDU support

| PDU (type) | Family | Encode | Decode | Godot API | Interop checked with |
|---|---|:-:|:-:|---|---|
| Entity State (1) | Entity Information | ✔ | ✔ (DIS 6 and 7) | `DisEntityStatePdu`, `DisEntity`, `DisEntityManager` | Open-DIS Python, opendis7-java, Wireshark, Wireshark samples |
| Fire (2) | Warfare | ✔ | ✔ | `DisFirePdu`, `DisEntity.fire()` | Open-DIS Python, opendis7-java, Wireshark |
| Detonation (3) | Warfare | ✔ | ✔ | `DisDetonationPdu`, `DisEntity.detonate()` | Open-DIS Python, opendis7-java, Wireshark |
| Remove Entity (12) | Simulation Management | ✔ | ✔ | `DisRemoveEntityPdu`, `DisExerciseControl` | Open-DIS Python, opendis7-java, Wireshark |
| Start/Resume (13) | Simulation Management | ✔ | ✔ | `DisStartResumePdu`, `DisExerciseControl` | Open-DIS Python, opendis7-java, Wireshark |
| Stop/Freeze (14) | Simulation Management | ✔ | ✔ | `DisStopFreezePdu`, `DisExerciseControl` | Open-DIS Python, opendis7-java, Wireshark |
| Acknowledge (15) | Simulation Management | ✔ | ✔ | `DisAcknowledgePdu`, `DisExerciseControl` | Open-DIS Python, opendis7-java, Wireshark |
| all other types | | raw bytes | raw bytes | `DisNetwork.send_bytes()`, `DisRecorder`, `DisPlayer` | recorded and replayed unchanged |

## Reproducing

From the repository root (PowerShell; `$godot` is the Godot 4.7 console executable):

```powershell
.\tools\.venv\Scripts\python.exe tools\golden.py          # Entity State golden PDUs (Open-DIS Python)
.\tools\.venv\Scripts\python.exe tools\golden_pdus.py     # all other types
.\tools\.venv\Scripts\python.exe tools\fetch_captures.py  # Wireshark samples + NPS capture into tools\.cache
& $godot --headless --path . --import
& $godot --headless --path . -s addons/gut/gut_cmdln.gd
```

The Wireshark test looks for tshark in the default install locations (Windows `C:\Program Files\Wireshark`,
Linux `/usr/bin`, macOS Homebrew / app bundle). The golden files are committed (`tests/golden/`), so the
byte-for-byte tests run without Python; the UDP tests need `tools/.venv` with `opendis`.

To check by eye: record with the demo (`--record x.pcapng`) or the command line tool and open the file in
Wireshark; `tshark -r x.pcapng -d udp.port==3000,dis -V` prints every field.
