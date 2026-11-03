# Third-Party Notices

Third-party code and data in this repository and in the release zip.
Tools used only at development time (Open-DIS Python as a test oracle, pymap3d, Wireshark) are listed for
reference; their code is not included.

## Compiled into the addon's libraries

The release zip ships this file next to the libraries (`addons/dis/THIRD_PARTY_NOTICES.md`), which reproduces the
notices below as their licenses require.

### open-dis-cpp 1.2.0
https://github.com/open-dis/open-dis-cpp, commit `d1288ac` (git submodule `thirdparty/open-dis-cpp`): the DIS 7
PDU classes used for Entity State, Fire, Detonation and the simulation management PDUs. Unmodified. BSD 2-Clause
License:

```
BSD 2-Clause License

Copyright (c) 2016, Open DIS
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### godot-cpp
https://github.com/godotengine/godot-cpp, commit `507ed9d` (git submodule `thirdparty/godot-cpp`): the C++ bindings
for Godot's GDExtension API, including generated headers from Godot's API description. Unmodified. MIT License:

```
Copyright (c) 2017-present Godot Engine contributors.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

On Linux the libraries also link the GCC C++ runtime (libstdc++, libgcc) statically, under the GCC Runtime Library
Exception, which places no conditions on the result.

## Included in the repository, not in the addon

### doctest 2.4.12
`thirdparty/doctest/doctest.h` — C++ unit tests only (`tests/cpp`), not part of the distributed addon.
MIT License, Copyright (c) 2016-2023 Viktor Kirilov. Full text: `thirdparty/doctest/LICENSE.txt`.

### GUT (Godot Unit Test) 9.7.1
`addons/gut/` — the test framework used by `tests/`; not part of the distributed addon. MIT License, Copyright (c)
2018 Tom "Butch" Wesley; full text `addons/gut/LICENSE.md`. Its fonts in `addons/gut/fonts/` are under the SIL Open
Font License 1.1 (`addons/gut/fonts/OFL.txt`).

### Demo routes (OpenStreetMap)
The land routes in `demo/scenario.gd` and `tools/demo_traffic.py` (a few dozen points along main roads in Üsküdar,
Istanbul) were taken from OpenStreetMap road data. © OpenStreetMap contributors, Open Database License (ODbL) 1.0,
https://www.openstreetmap.org/copyright. Not part of the distributed addon.

## Included in the repository and in the addon

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

## Development-time tools

- **Open-DIS Python** (BSD-2-Clause, https://github.com/open-dis/open-dis-python) — development-time test oracle;
  if its test fixtures are copied, its copyright notice goes here.
