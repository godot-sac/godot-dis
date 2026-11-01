# Third-Party Notices

Entries are added when third-party code or data is copied into this repository.
Tools used only at development time (Open-DIS Python as a test oracle, pymap3d, Wireshark) are listed for
reference; their code is not included.

## Included

### doctest 2.4.12
`thirdparty/doctest/doctest.h` — C++ unit tests only (`tests/cpp`), not part of the distributed addon.
MIT License, Copyright (c) 2016-2023 Viktor Kirilov. Full text: `thirdparty/doctest/LICENSE.txt`.

### SISO-REF-010 v37 (Enumerations for Simulation Interoperability)
`addons/dis/data/enums.json` and `addons/dis/data/entity_types.json` are derived from the machine-readable
`SISO-REF-010.xml` (v37, 2026-05-25) by `tools/enums.py`. **Not covered by this project's MIT license.**

Reprinted with permission from SISO Inc.

Copyright © 2026 by the Simulation Interoperability Standards Organization, Inc. All rights reserved.
The "Schema and API" material is licensed by SISO under a general, royalty-free license to copy, distribute, display
and make derivative works for all purposes, provided that any use contains the attribution
"Reprinted with permission from SISO Inc." Only this material is used; the document renditions (licensed for
noncommercial purposes only) are not. Source: SISO_RID_1003262 (http://www.sisostds.org), fetched from a
version-tagged byte-exact mirror (https://github.com/ngjunsiang/siso-ref-010-xml, tag `v37`), SHA-256
`eaafe0b868e5fe6b3497c915226f312ca9853c242fae11d2f6afcbfb53c8be8c`. The same notice ships with the addon in
`addons/dis/data/NOTICE.txt`.

`addons/dis/data/sidc_map.json` (DIS category → MIL-STD-2525D symbol) is this project's own table (MIT); it uses
SISO-REF-010 category names as labels.

## Test inputs downloaded, not included

`tools/fetch_captures.py` downloads these to `tools/.cache/captures/` for the tests; they are not committed or
distributed.

- **Wireshark SampleCaptures** `DIS_EntityState_1.pcapng`, `DIS_EntityState_2.pcapng`,
  `DIS_EnvironmentalProcess.pcapng`, `DIS_Signal.pcapng`, `DIS_signal_and_transmitter.pcapng` from
  https://wiki.wireshark.org/SampleCaptures. No license is stated for these files.
- **NPS MOVES MV3500** `AllPduSenderWiresharkCapture.pcapng` from https://gitlab.nps.edu/Savage/NetworkedGraphicsMV3500
  (`examples/src/OpenDis7Examples/`), under the repository's "Open-Source Software License for NPS MOVES Savage
  Research Group" (BSD-style), Copyright (c) 1995-2024 held by the author(s).

Wireshark / tshark (GPL-2.0) is run as a separate program by the tests; none of its code is used or read.

## Planned

- **open-dis-cpp** (BSD-2-Clause, https://github.com/open-dis/open-dis-cpp) — compiled into the extension.
- **godot-cpp** (MIT, https://github.com/godotengine/godot-cpp) — compiled into the extension.
- **Open-DIS Python** (BSD-2-Clause, https://github.com/open-dis/open-dis-python) — development-time test oracle;
  if its test fixtures are copied, its copyright notice goes here.
