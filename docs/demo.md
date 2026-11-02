# Demo: a two-sided exercise around Ankara

![Blue side's view: own units, the red side's units and civilian traffic from Open-DIS Python](images/demo_blue.png)

`demo/main.tscn` is one simulation of a small exercise. Run two copies — Blue and Red — and the civilian traffic
script, and every copy shows all units on a tactical map:

| Who | Site | Force | Units |
|---|---|---|---|
| Godot `--side blue` | 1 | Friendly | 2 × Leopard 2A4TR, ACV 300, attack helicopter, F-4 (Türkiye, SISO-REF-010 country 218) |
| Godot `--side red` | 2 | Opposing | 2 tanks, AFV, self-propelled artillery, air defense, fighter (generic types) |
| `tools/demo_traffic.py` (Open-DIS Python) | 3 | Neutral | airliner at 10 km on a racetrack, two cars, a bus |

Each copy publishes its own units with `DisEntity` (dead reckoning thresholds, heartbeat) and shows everyone's with
`DisEntityManager`: names from `DisEnums`, MIL-STD-2525D symbols from godot-milsymbol. Everything goes over
multicast 239.1.2.3:3000, so the copies can share the port on one computer (see docs/networking.md) or run on
different computers of a LAN.

## Run it (PowerShell, from the repo root)
Once: link godot-milsymbol (without it the map shows plain dots) and import.
```powershell
cd path\to\godot-dis
.\tools\.venv\Scripts\python.exe tools\link_milsymbol.py
& "C:\path\to\godot.exe" --headless --path . --import
```

Then in three terminals:
```powershell
& "C:\path\to\godot.exe" --path . res://demo/main.tscn -- --side blue
```
```powershell
& "C:\path\to\godot.exe" --path . res://demo/main.tscn -- --side red
```
```powershell
.\tools\.venv\Scripts\python.exe tools\demo_traffic.py
```

Mouse wheel zooms, dragging pans; the side panel lists every unit with its source (`own` or site:application:entity),
position and speed, and how many PDUs each own unit has sent; below it an event log. Close one window and its
units disappear from the other at once (it sends a "deactivated" PDU on exit).

## Keys
| Key | What happens | PDUs |
|---|---|---|
| F | the next own unit fires at the nearest enemy within 30 km; the round lands after up to 4 s (shortened time of flight) with a 70 % chance to hit | Fire, then Detonation |
| X | asks the owner of the remote unit under the mouse to remove it; the owner frees it and answers | Remove Entity, Acknowledge, final Entity State |
| S | stops (freezes) the whole exercise: every copy and the Python traffic stop moving and sending, a banner shows who stopped it | Stop/Freeze, Acknowledge, Comment |
| R | resumes it | Start/Resume, Acknowledge, Comment |
| T | a 2 s radio call from the helicopter on 243.000 MHz: waves on its symbol in every copy | Transmitter, Signal |

A hit sets the target's damage appearance to "destroyed" (it stops, its symbol shows the 2525D destroyed status in
every copy). Fire lines, bursts and the exercise state are drawn by `demo/map_effects.gd` on top of the map.
The exercise control is `DisExerciseControl` with `pause_tree`: stopping pauses the scene tree except the UI,
network and control node (see docs/simulation-management.md).

## Options
After `--`: `--side blue|red`, `--seconds N` (quit after N s, printing PDU counts by type), `--screenshot file.png`
(saved at quit), `--port N` (a separate exercise, e.g. to test next to a running demo), `--record file.pcapng`
(or `.pdulog`; replay it with `demo/replay.tscn`, see docs/recording.md). For unattended runs: `--auto-fire S`
(fire every S seconds), `--stop-at S`, `--resume-at S`, `--remove site:app:entity@S`, `--radio-at S`.
`--capture DIR` saves the window as PNG frames, 10 per second of real time, for `tools/make_media.py`
(see docs/release.md).

An unattended check of everything at once, on port 3297 so it doesn't disturb a running demo (three terminals):
```powershell
.\tools\.venv\Scripts\python.exe tools\demo_traffic.py --port 3297 --seconds 25
& $godot --headless --path . res://demo/main.tscn -- --side red --port 3297 --seconds 22 --auto-fire 2 --stop-at 12 --resume-at 15 --remove 3:1:2@18
& $godot --headless --path . res://demo/main.tscn -- --side blue --port 3297 --seconds 20 --auto-fire 3 --record user://exercise.pcapng
```
Each prints what it received by PDU type; then `& $godot --path . res://demo/replay.tscn -- --file user://exercise.pcapng`
replays the blue side's recording.

`tools/demo_traffic.py` options: `--start S` (skip S seconds ahead along the routes; the airliner reaches its first
turn after ~400 s), `--rate HZ` (updates per second, default 1), `--port N`, `--seconds N`. It sends dead reckoning
algorithm 4 (RVW) with acceleration and turn rate, so receivers follow the vehicles through turns too. It also obeys
Stop/Freeze, Start/Resume and Remove Entity addressed to site 3 and answers them with Open-DIS Acknowledge PDUs,
and the airliner's radio talks on 118.100 MHz for 3 s every 20 s (Transmitter and Signal PDUs).

## Watch it in Wireshark
Capture on the loopback adapter (one computer) or the LAN adapter with filter `udp.port == 3000`: Entity State PDUs
from sites 1, 2 and 3, and Fire, Detonation and simulation management PDUs when you press the keys above (or open
a `--record` file directly). A constant-speed tank sends one PDU every 5 s (heartbeat) plus one per turn; aircraft in a
turn send a few per second, because the routes are polygons.

## Files
- `demo/main.tscn`, `demo/demo_main.gd` — network, origin, manager, own units, UI
- `demo/scenario.gd` — the units of each side; `demo/unit_mover.gd` — moves a unit along its route
- `demo/tactical_map.gd` — the map (grid, symbols, labels, trails); `demo/map_effects.gd` — fire lines, bursts,
  destroyed markers and the exercise banner drawn over it (optional: the map works without it)
- `demo/replay.tscn`, `demo/replay_main.gd` — the replay viewer (docs/recording.md)
- `tools/demo_traffic.py` — civilian traffic with Open-DIS Python
