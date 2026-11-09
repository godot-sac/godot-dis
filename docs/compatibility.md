# Compatibility

What each godot-dis version was actually tried with. "Declared" means the extension says it loads there
(`compatibility_minimum` in `dis.gdextension`), not that it was run.

## godot-dis 0.2.0 and 0.1.0
godot-cpp 10 built against the Godot 4.3 extension API, open-dis-cpp (DIS 7), DIS versions 5–7 read.

| Godot | Platform | Build | Status | What was run |
|---|---|---|---|---|
| 4.7.2 | Windows 11 x86_64 | MSVC 2022 | ✅ tested | GUT 113/113, doctest 24/24, two-sided demo and replay demo, release zip in an empty project (`check_package.py`, also next to godot-jsbsim 0.1.0), [getting-started](getting-started.md) project, interop with Open-DIS Python, opendis7-java captures and tshark |
| 4.7.2 | Linux x86_64: Ubuntu 24.04 (WSL 2), glibc 2.39 | GCC 13 | ✅ tested | GUT 111 + 2 pending (no tshark), doctest, the same release zip in an empty project (alone and next to godot-jsbsim 0.1.0), exported symbols (`check_exports.py`), getting-started followed step by step in a clean directory |
| 4.7.2 | Windows 11 x86_64 | MSVC 2022 | ✅ tested (0.2.0) | GUT 133/133, golden PDUs of the five new types, opendis7-java capture 19/19 types |
| 4.7.2 | Windows Server 2022, Linux x86_64: Ubuntu 22.04 (CI) | MSVC 2022, GCC 11 | ✅ tested (0.1.0, 0.2.0) | build, exported symbols, doctest, GUT, class reference check, release zip in an empty project |
| 4.7.2 | macOS universal (arm64 + x86_64) | Apple Clang (CI) | ⏳ not in 0.1 | Built once by CI (2026-11-08), never run; removed from CI and the zip until it can be tested |
| 4.3 – 4.6 | any | — | ❔ declared | not tried |
| 4.7.2 .NET (C#) build | any | — | ❔ untested | not tried |

## Together with the other add-ons
| Add-on | Version | Platforms | What was run |
|---|---|---|---|
| godot-jsbsim | 0.1.0 | Windows, Linux (WSL) | both release zips in one empty project: J3Cub flies, entity over UDP (`check_package.py --with`); JSBSim's DIS output shown by the demo |
| godot-geo | 0.1.0 | Windows | `DisEntityManager.ground_path` on `GeoTerrain`, godot-geo's 3D DIS demo |
| godot-milsymbol | 0.1.0 | Windows | 2525D symbols on the demo's tactical map (`DisEnums.entity_type_to_sidc()`) |
| godot-sim-demo | — | Windows, Linux (WSL) | all four add-ons in one project |

Notes:
- **Linux and other GDExtensions:** before 0.1.0, the Linux library exported the statically linked libstdc++, and
  godot-jsbsim crashed in the same project (std::regex in JSBSim bound to our copy). The library now exports only
  its entry point; `tools/check_exports.py` checks it in CI. See [building](building.md#linux-symbol-visibility).
- **Linux libraries** of the release are built on Ubuntu 22.04 (CI), so they should run on glibc 2.35 or newer;
  older systems are not tried. The zips built locally for testing came from Ubuntu 24.04.
- **Windows** needs the Microsoft Visual C++ Redistributable 2015–2022 (x64), usually already installed.
- **Networking** was tested on one computer (loopback, broadcast and multicast loop back), not between computers
  of a real exercise network. Multicast in WSL 2 works between programs inside WSL.
- **Exported games:** tested with Godot 4.7.2 release templates on Windows and Linux (2026-11-09, release zip
  0.1.0, in godot-sim-demo with godot-jsbsim and godot-geo; see godot-sim-demo's [export guide](https://github.com/godot-sac/godot-sim-demo/blob/main/docs/export.md)). Add `*.json` to the export's include
  filter: the enumeration tables in `addons/dis/data/` are read as files.
- Only x86_64 on Windows and Linux; no ARM64 Linux / Windows, Android, iOS or Web builds.

Tried something not in this table? Please open an issue with the Godot version, the platform and what happened.
