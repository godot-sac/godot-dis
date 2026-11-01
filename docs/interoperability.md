# Interoperability

There is no DIS certification or plugfest today, so godot-dis is checked the way DIS implementations check each
other in practice: byte-for-byte against independent implementations, against real captures, and with Wireshark's
DIS dissector as a neutral decoder. Every check below is an automated test that runs with the GUT suite (checks
that need a tool or a download are *pending* when it is missing).

## Results (2026-11-01, godot-dis 0.1.0-dev, Windows 11 and Ubuntu 24.04)

| Against | Version | What | PDU types | Result | Test |
|---|---|---|---|---|---|
| Open-DIS Python | `opendis` 1.1.0 | Golden PDUs written by Open-DIS: our `encode()` gives identical bytes, our `decode()` gives every field | Entity State (31 cases), Fire (9), Detonation (17), Collision (6), Create Entity (3), Remove Entity (6), Start/Resume (8), Stop/Freeze (15), Acknowledge (30), Set Data (3), Data (3), Comment (5), Transmitter (5), Signal (5) | ✔ 146 / 146 | `test_entity_state_pdu.gd`, `test_pdus_golden.gd` |
| Open-DIS Python | 1.1.0 | Over UDP, Python → Godot: all golden PDUs arrive, decode and re-encode identically; Godot → Python: Open-DIS decodes each as the right class and serializes it back to the same bytes | all 14 | ✔ | `test_network.gd` |
| opendis7-java | as captured on 2021-08-14 | NPS MV3500 `AllPduSenderWiresharkCapture.pcapng` (one PDU of each of the 72 DIS 7 types): every type we support decodes and re-encodes to the same bytes | all 14 | ✔ 14 / 14 (see note 1) | `test_captures.gd` |
| Wireshark DIS dissector | tshark 4.6.9 | A pcapng written by `DisRecording` with all 146 golden PDUs: right PDU type and length for every frame, no malformed packets (one known exception, note 4), no expert warnings; field values (marking, IDs, country, event number, range) as written | all 14 | ✔ | `test_wireshark.gd` |
| Wireshark SampleCaptures | wiki files, SHA-256 pinned | Captures from other DIS senders decode and re-encode identically, including a DIS 5 radio capture; Environmental Process is read and replayed (a type we don't decode) | Entity State 289, Signal 801, Transmitter 10 | ✔ | `test_captures.gd`, `test_radio.gd` |
| Open-DIS Python application | demo traffic | Live: a Godot manager stops, resumes and removes entities of an Open-DIS Python application, which answers with Open-DIS Acknowledge PDUs; its radio (Transmitter, Signal) shows on the Godot map; Godot ↔ Godot the same, plus Comment | Start/Resume, Stop/Freeze, Remove Entity, Acknowledge, Comment, Transmitter, Signal | ✔ (manual run) | `docs/demo.md` |
| GRILL DIS for Unreal / Unity | — | not tested (no Unreal / Unity setup) | | — | |

Notes
1. The opendis7-java capture leaves the PDU **length field 0** in every PDU. godot-dis accepts a length of 0 as
   "the size of the datagram" when reading, and always writes the real length; the re-encoded bytes are compared
   with the length field filled in. A live test against current opendis7-java was not run (it needs JDK 21 and
   Ant; the distributed jar is about 300 MB).
2. Open-DIS Python writes the Stop/Freeze padding as a signed 16-bit number and the Clock Time hour as unsigned;
   both are the same bytes on the wire. It does not fill the length field either (the golden generator sets it).
3. Wireshark's dissector source (GPL) was not read; Wireshark is used only as a program.
4. Wireshark 4.6.9 reports a Signal PDU without any data (data length 0) as malformed ("exception occurred"); the
   PDU is complete and Open-DIS Python writes the same bytes. The test lists it as a known dissector limit.
5. The DIS 5 radio capture writes protocol family 0 in the header and pads its Transmitter PDUs with 45 zero
   bytes beyond the length its fields need; godot-dis keeps the family byte and the extra bytes
   (`trailing_bytes`) so they re-encode identically.
6. Open-DIS Python does not pad Signal data to 32 bits; the golden Signal cases use data sizes that need no
   padding. open-dis-cpp has no DIS 7 Transmitter and rounds variable datum lengths down to whole bytes, so
   these PDUs use godot-dis's own codec.

## PDU support

| PDU (type) | Family | Encode | Decode | Godot API | Interop checked with |
|---|---|:-:|:-:|---|---|
| Entity State (1) | Entity Information | ✔ | ✔ | `DisEntityStatePdu`, `DisEntity`, `DisEntityManager` | Open-DIS Python, opendis7-java, Wireshark, Wireshark samples |
| Fire (2) | Warfare | ✔ | ✔ | `DisFirePdu`, `DisEntity.fire()` | Open-DIS Python, opendis7-java, Wireshark |
| Detonation (3) | Warfare | ✔ | ✔ | `DisDetonationPdu`, `DisEntity.detonate()` | Open-DIS Python, opendis7-java, Wireshark |
| Collision (4) | Entity Information | ✔ | ✔ | `DisCollisionPdu` | Open-DIS Python, opendis7-java, Wireshark |
| Create Entity (11) | Simulation Management | ✔ | ✔ | `DisCreateEntityPdu`, `DisExerciseControl` | Open-DIS Python, opendis7-java, Wireshark |
| Remove Entity (12) | Simulation Management | ✔ | ✔ | `DisRemoveEntityPdu`, `DisExerciseControl` | Open-DIS Python, opendis7-java, Wireshark |
| Start/Resume (13) | Simulation Management | ✔ | ✔ | `DisStartResumePdu`, `DisExerciseControl` | Open-DIS Python, opendis7-java, Wireshark |
| Stop/Freeze (14) | Simulation Management | ✔ | ✔ | `DisStopFreezePdu`, `DisExerciseControl` | Open-DIS Python, opendis7-java, Wireshark |
| Acknowledge (15) | Simulation Management | ✔ | ✔ | `DisAcknowledgePdu`, `DisExerciseControl` | Open-DIS Python, opendis7-java, Wireshark |
| Set Data (19) | Simulation Management | ✔ | ✔ | `DisSetDataPdu` | Open-DIS Python, opendis7-java, Wireshark |
| Data (20) | Simulation Management | ✔ | ✔ | `DisDataPdu` | Open-DIS Python, opendis7-java, Wireshark |
| Comment (22) | Simulation Management | ✔ | ✔ | `DisCommentPdu` | Open-DIS Python, opendis7-java, Wireshark |
| Transmitter (25) | Radio Communications | ✔ | ✔ | `DisTransmitterPdu`, `DisRadioMonitor` | Open-DIS Python, opendis7-java, Wireshark, Wireshark samples (DIS 5) |
| Signal (26) | Radio Communications | ✔ | ✔ | `DisSignalPdu`, `DisRadioMonitor` | Open-DIS Python, opendis7-java, Wireshark, Wireshark samples (DIS 5) |
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
