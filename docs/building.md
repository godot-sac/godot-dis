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

### Linux symbol visibility
godot-cpp links libstdc++ statically on Linux, and a GCC shared library exports every global symbol by default. If
our copy of libstdc++ is exported, another extension in the same project that uses the system's shared libstdc++
(godot-jsbsim's `libJSBSim.so.1`) binds part of its `std::locale` / `std::regex` code to it and crashes. So the
library is linked with a version script, `src/linux_exports.map`, that exports only `godot_dis_library_init`.
Check it after a build (CI does):
```sh
python3 tools/check_exports.py      # "1 exported symbols, ok" per library
```

## Release zip
```sh
python tools/package_addon.py --version 0.1.0     # build/package/godot-dis-0.1.0.zip, every platform that is built
python tools/check_package.py build/package/godot-dis-0.1.0.zip --godot <godot> [--with other-addon.zip]
```
The zip has `addons/dis/` at its root: `dis.gdextension`, the debug and release libraries, data, icons, the CLI
tool, `LICENSE` and `THIRD_PARTY_NOTICES.md` (no linker files). `check_package.py` installs it in a new, empty
project and checks the first import, the classes, the enumeration data and an entity sent over UDP on localhost.
For a Windows + Linux zip on Windows, copy the Linux libraries (built in WSL) to `addons/dis/bin/linux/` first.
See [release](release.md).

## Continuous integration
`.github/workflows/ci.yml` runs on every push and pull request (once the repository is on GitHub):
- Windows (MSVC), Linux (Ubuntu 22.04, GCC) and macOS (universal): debug and release builds, C++ unit tests.
- Windows and Linux: downloads Godot 4.7.2 and the public sample captures, and runs the GUT tests, including the
  Open-DIS Python interop tests; Linux also installs tshark for the Wireshark test.
  Broadcast / multicast tests become "pending" if the runner's network does not loop them back.
- Linux: checks that the library exports only its entry point (`tools/check_exports.py`).
- The `package` job packs the release zip with the libraries of all three platforms (`tools/package_addon.py`,
  artifact `godot-dis-<version>`) and checks it in an empty project on Linux (`tools/check_package.py`).
- On a `vX.Y.Z` tag the `release` job creates a draft GitHub release with that zip ([release](release.md)).

The Linux job's steps were checked locally in WSL (Ubuntu 24.04, GCC 13): build, exported symbols, 24 C++ tests, 113 GUT
tests (the 2 Wireshark tests pending without tshark), and the release zip in an empty project.

## Test
```powershell
$godot = "C:\path\to\Godot_v4.7.2-stable_win64_console.exe"   # your Godot path
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
