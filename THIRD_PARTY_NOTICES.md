# Third-Party Notices

Entries are added when third-party code or data is copied into this repository.
Tools used only at development time (Open-DIS Python as a test oracle, pymap3d, Wireshark) are listed for
reference; their code is not included.

## Included

### doctest 2.4.12
`thirdparty/doctest/doctest.h` — C++ unit tests only (`tests/cpp`), not part of the distributed addon.
MIT License, Copyright (c) 2016-2023 Viktor Kirilov. Full text: `thirdparty/doctest/LICENSE.txt`.

## Planned

- **open-dis-cpp** (BSD-2-Clause, https://github.com/open-dis/open-dis-cpp) — compiled into the extension.
- **godot-cpp** (MIT, https://github.com/godotengine/godot-cpp) — compiled into the extension.
- **SISO-REF-010 (Enumerations for Simulation Interoperability), v37** — enumeration values exported to
  `addons/dis/data/`. Reprinted with permission from SISO Inc. Not covered by this project's MIT license.
- **Open-DIS Python** (BSD-2-Clause, https://github.com/open-dis/open-dis-python) — development-time test oracle;
  if its test fixtures are copied, its copyright notice goes here.
