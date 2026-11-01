# Recording and playback

Record what goes over the DIS network, play it back into a Godot scene (after action review, debugging, demos) or
onto the network again, and exchange recordings with Wireshark and other tools.

- `DisRecorder` (node): writes everything a `DisNetwork` receives and sends to a file while the game runs.
- `DisRecording` (object): a recording in memory — load, save, inspect, convert.
- `DisPlayer` (node): plays a recording back with speed, pause, seek and loop.
- `addons/dis/tools/dis_cli.gd`: the same from the command line, without a scene.
- `demo/replay.tscn`: a replay viewer on the tactical map.

## File formats

| Format | Read | Write | What for |
|---|:-:|:-:|---|
| **pcapng** (`.pcapng`) | ✔ | ✔ | the network capture format of Wireshark, tshark, dumpcap and most capture tools |
| pcap (`.pcap`) | ✔ | | older captures (microsecond and nanosecond variants, both byte orders) |
| **pdulog** (`.pdulog`) | ✔ | ✔ | plain text, one datagram per line; easy to read, diff, grep and generate |

The format is detected from the first bytes when reading and taken from the file extension when writing
(`.pdulog` → pdulog, anything else → pcapng).

**pcapng as written by godot-dis:** one Ethernet interface with microsecond timestamps; every datagram gets
synthesized Ethernet, IPv4 and UDP headers with the real sender address and port, the multicast group / broadcast
address / our address as destination, the standard multicast or broadcast MAC address (placeholders otherwise),
and valid IPv4 and UDP checksums. Wireshark decodes the PDUs with its DIS dissector (on another port than 3000:
*Decode As… → UDP port → DIS*, or `tshark -d udp.port==3001,dis`).

**Reading captures:** Ethernet (with VLAN tags), the BSD loopback type that Npcap uses on Windows, raw IPv4 / IPv6
and Linux "cooked" (SLL, SLL2) link types are understood. Only UDP datagrams that start with a plausible DIS
header are kept (TCP, ARP, DNS ... are skipped and counted, see `get_read_statistics()`); IPv4 fragments are skipped
(DIS PDUs fit in one datagram). Several PDUs bundled in one datagram stay together as one recorded packet.

**pdulog:**
```
# DIS PDU log 1 (godot-dis): <seconds since start> <source> <destination> <PDU hex>
# start: 1793534400.123456
0.000000 192.168.1.20:3000 239.1.2.3:3000 0701010100000000009000000001000100010301...
0.503112 192.168.1.21:3000 239.1.2.3:3000 07010201...
```
Lines starting with `#` are comments; `# start:` is the UNIX time the first column counts from (0 if missing).
IPv6 addresses are written in brackets (`[fe80::1]:3000`). The idea of a timestamped text log with one PDU per line
follows opendis7-java's `.dislog` plain-text mode, but the format is our own and not compatible with it.

## Recording in a game

```gdscript
var recorder := DisRecorder.new()
add_child(recorder)                      # uses the first DisNetwork in the scene (or set network_path)
recorder.start("user://exercise.pcapng") # or set `path` and `autostart`
...
recorder.stop()                          # also on exit; the file is flushed every flush_interval (1 s) anyway
```

The recorder sees each datagram before the network's filters, so other exercises and malformed packets are recorded
too (readers skip what is not DIS). With `record_sent` (default) the PDUs this application sends are recorded as
well; when they come back to us (broadcast, multicast loopback) the second copy is not recorded.

The exercise demo records with `--record <file>`:
```powershell
& $godot --path . res://demo/main.tscn -- --side blue --record user://exercise.pcapng
```

## Playing back

```gdscript
var player := DisPlayer.new()
add_child(player)              # uses the first DisNetwork and DisEntityManager in the scene
player.load("user://exercise.pcapng")
player.speed = 4.0
player.play()
player.seek(120.0)             # seconds since the start of the recording
```

- `output = OUTPUT_LOCAL` (default): every packet is handed to the `DisNetwork` with `inject_packet()` as if it had
  just arrived — `pdu_received`, `entity_state_received`, `fire_received` ... fire, so a `DisEntityManager` shows the
  recorded entities and your own handlers react. The network does not need to be started; the exercise and
  own-packet filters are skipped.
- `OUTPUT_NETWORK`: the packets are sent with the network's settings (another port, multicast group or exercise:
  `exercise_id_override`). `OUTPUT_BOTH` does both.
- Speed scales the entity manager's dead-reckoning clock (`DisEntityManager.time_scale`), so entities move
  smoothly between updates at 0.25× as well as at 16×; while paused they stand still.
- `seek()` rebuilds the picture at once: the entity manager is cleared and each entity's last Entity State PDU
  before the new position is applied as if it had arrived that long ago. Fire / Detonation and other events in the
  skipped part are not replayed.
- `advance(seconds)` plays forward without the wall clock (for tests and tools). Signals: `packet_played`,
  `seeked`, `finished`.

## Replay viewer

```powershell
& $godot --path . res://demo/replay.tscn -- --file user://exercise.pcapng
```
Space plays / pauses, Left / Right jump 10 s, Up / Down change the speed (0.25× to 32×), Home goes to the start;
the slider seeks. The event list shows fire, detonations and simulation management with recording time.
Options: `--speed X`, `--seek S`, `--loop`, `--rebroadcast` (also send onto multicast 239.1.2.3:3000),
`--seconds N --screenshot file.png`. It opens any capture, for example the Wireshark sample captures
(`tools/fetch_captures.py`), but the map is centred on Ankara.

## Command line

```powershell
$cli = "-s", "res://addons/dis/tools/dis_cli.gd", "--"
& $godot --headless --path . @cli record traffic.pcapng --mode multicast --group 239.1.2.3 --seconds 60
& $godot --headless --path . @cli info traffic.pcapng
& $godot --headless --path . @cli dump traffic.pcapng --limit 20
& $godot --headless --path . @cli play traffic.pcapng --mode multicast --speed 2 --loop
& $godot --headless --path . @cli convert traffic.pcapng traffic.pdulog
```
Relative file names are relative to the project folder. Network options: `--mode broadcast|multicast|unicast`
(default broadcast), `--port` (3000), `--group`, `--destination`, `--interface`. `play` also takes `--start S` and
`--exercise N`; `info` and `dump` take `--port N` to keep one UDP port of a mixed capture.

```
file:       C:/.../traffic.pcapng
start:      2026-11-01 17:18:41 UTC
duration:   4.540 s
packets:    40 DIS datagrams (of 40 frames; skipped: 0 not UDP, 0 fragments, 0 not DIS, 0 other ports, 0 cut off)
exercises:  1
sites:      3:1
entities:   4 (Entity State PDUs)
PDU types:
    1  Entity State                 40
```

## Sample captures

`tools/fetch_captures.py` downloads public DIS captures to `tools/.cache/captures/` (SHA-256 checked): the Wireshark
SampleCaptures DIS files (Entity State, Environmental Process, Signal, Transmitter) and the NPS MV3500
`AllPduSenderWiresharkCapture.pcapng` (one PDU of each of the 72 DIS 7 types, sent by opendis7-java). They are used
by the tests and are not part of this repository; see [interoperability.md](interoperability.md).
