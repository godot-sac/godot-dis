# Building

## Requirements
- Python 3.10+ (for SCons and the test oracle)
- Windows: Visual Studio 2022 with the "Desktop development with C++" workload (MSVC 14.4x)
- Linux: GCC 11+ or Clang 14+
- Godot 4.3 or newer to run it (developed with 4.7.2)

## First time
PowerShell (Windows), from the repo root:
```powershell
git clone --recursive <repo>          # or: git submodule update --init
py -3.13 -m venv tools\.venv
.\tools\.venv\Scripts\python.exe -m pip install -r tools\requirements.txt
```
Linux/macOS: `python3 -m venv tools/.venv && tools/.venv/bin/pip install -r tools/requirements.txt`.

## Build
```powershell
.\tools\.venv\Scripts\scons.exe -j16 target=template_debug     # editor and debug exports
.\tools\.venv\Scripts\scons.exe -j16 target=template_release   # release exports
```
Linux/macOS: `tools/.venv/bin/scons -j16 target=template_debug`.

Output: `addons/dis/bin/<platform>/libgodot_dis.<platform>.<target>.<arch>.<ext>`.
The first build compiles godot-cpp (about a minute); later builds only recompile changed files.
After rebuilding, restart the Godot editor if it had the old library loaded.
macOS: add `arch=universal` (one library for Intel and Apple Silicon, the path `dis.gdextension` expects).

## Continuous integration
`.github/workflows/ci.yml` runs on every push and pull request (once the repository is on GitHub):
- Windows (MSVC), Linux (Ubuntu 22.04, GCC) and macOS (universal): debug and release builds, C++ unit tests.
- Windows and Linux: downloads Godot 4.7.2 and the public sample captures, and runs the GUT tests, including the
  Open-DIS Python interop tests; Linux also installs tshark for the Wireshark test.
  Broadcast / multicast tests become "pending" if the runner's network does not loop them back.
- A final job packs `addons/dis` with the libraries of all three platforms (artifact `godot-dis-addon`).

The Linux job's steps were checked locally in WSL (Ubuntu 24.04, GCC 13): build, 24 C++ tests, 108 GUT tests
(the 2 Wireshark tests pending without tshark).

## Test
```powershell
$godot = "C:\path\to\godot.exe"   # your Godot path
& $godot --headless --path . --import
& $godot --headless --path . -s addons/gut/gut_cmdln.gd
```
Optional parts of the suite, "pending" when missing: the UDP interop tests need `tools/.venv` with Open-DIS Python;
the sample-capture tests need `.\tools\.venv\Scripts\python.exe tools\fetch_captures.py` (downloads public DIS
captures to `tools/.cache/captures`); the Wireshark test needs tshark (Wireshark installed in its default place,
or `apt install tshark`). See docs/interoperability.md.

C++ unit tests (doctest, no Godot needed; coordinate math, dead reckoning, capture file formats):
```powershell
.\tools\.venv\Scripts\scons.exe tests
```

Regenerate oracle data (only when the cases change):
```powershell
.\tools\.venv\Scripts\python.exe tools\golden.py       # Entity State PDUs (Open-DIS Python)
.\tools\.venv\Scripts\python.exe tools\golden_pdus.py  # Fire, Detonation, simulation management PDUs
.\tools\.venv\Scripts\python.exe tools\geo_golden.py   # coordinates (pymap3d, Open-DIS Python)
.\tools\.venv\Scripts\python.exe tools\dr_golden.py    # dead reckoning (Open-DIS Python)
```

Addon data (SISO-REF-010 names and the 2525D symbol map; see docs/enumerations.md):
```powershell
.\tools\.venv\Scripts\python.exe tools\enums.py
.\tools\.venv\Scripts\python.exe tools\sidc_map.py
node tools\check_sidc_map.mjs
```

Performance (1000 entities at 5 Hz over loopback for 5 s):
```powershell
& $godot --headless --path . -s res://tools/perf_entities.gd -- 1000 5 5
```

## Check bytes with the oracle
```powershell
& $godot --headless --path . -s res://tools/dump_probe.gd -- probe.bin
.\tools\.venv\Scripts\python.exe tools\inspect_pdu.py probe.bin
```
