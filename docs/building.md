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

## Test
```powershell
$godot = "C:\path\to\godot.exe"   # your Godot path
& $godot --headless --path . --import
& $godot --headless --path . -s addons/gut/gut_cmdln.gd
```

C++ unit tests (doctest, no Godot needed; coordinate math and dead reckoning):
```powershell
.\tools\.venv\Scripts\scons.exe tests
```

Regenerate oracle data (only when the cases change):
```powershell
.\tools\.venv\Scripts\python.exe tools\golden.py       # Entity State PDUs (Open-DIS Python)
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
