# Networking

`DisNetwork` is a `Node` that owns one UDP socket. Add it to the scene, set the properties, connect to
its signals. It opens the socket on `_ready` (`autostart`) and reads every waiting packet each frame.

```gdscript
@onready var network: DisNetwork = $DisNetwork

func _ready() -> void:
	network.entity_state_received.connect(_on_entity_state)

func _on_entity_state(pdu: DisEntityStatePdu) -> void:
	print(pdu.marking, " at ", pdu.location_ecef)

func publish(pdu: DisEntityStatePdu) -> void:
	network.send_pdu(pdu)  # stamps the network's exercise_id
```

## Modes
| Mode | Sends to | Receives |
|---|---|---|
| Broadcast | `destination_address` (default 255.255.255.255) | everything on `port` |
| Unicast | `destination_address` | everything on `port` |
| Multicast | `multicast_group` | joins `multicast_group` on `multicast_interface`, or on every interface with a routable IPv4 address |

`destination_port` 0 means "same as `port`". DIS commonly uses port 3000.

## Filters
- `exercise_id` + `filter_exercise`: PDUs from other exercises are counted as ignored.
- `site_id` / `application_id` + `ignore_own_packets`: Entity State PDUs whose entity ID starts with this
  site/application are treated as our own echo (broadcast and multicast loop back to the sender).
  Give every simulation on the network a different site/application pair.
- Malformed packets emit `packet_rejected(reason, sender_ip)` and never reach `open-dis-cpp`.

`get_statistics()` returns received / sent / rejected / ignored counters and the joined multicast interfaces.

## Windows notes (found while testing)
- **Limited broadcast (255.255.255.255) may leave through the wrong adapter** when VPN (NordVPN, Tailscale),
  WSL or other virtual adapters exist, so other machines on the LAN never see it. Use the subnet-directed
  broadcast address instead, e.g. `192.168.1.255` for a 192.168.1.x/24 network.
- **Multicast** joins fail on adapters that only have a link-local `169.254.x.x` address; `DisNetwork` skips
  them. Set `multicast_interface` to one adapter name (`IP.get_local_interfaces()`) to be explicit.
- The first time Godot opens a UDP port, Windows Defender Firewall may ask for permission. Allow it on
  private networks, or broadcast/multicast from other machines will not arrive.

## Watching packets in Wireshark
Wireshark (GPL) is used only as a tool here.
1. Install Wireshark (`winget install --id WiresharkFoundation.Wireshark`) and Npcap from https://npcap.com
   (the Wireshark MSI does not include it). For traffic on 127.0.0.1, capture on the "Adapter for loopback
   traffic capture" interface (older Npcap versions need "Support loopback traffic" enabled in the installer;
   1.90 installs it without asking).
2. Start capturing, filter with `udp.port == 3000`.
3. Send some entities:
   ```powershell
   cd path\to\godot-dis
   & "C:\path\to\godot.exe" --headless --path . -s res://tools/send_entities.gd -- 127.0.0.1 3000 30
   ```
   (or `192.168.1.255 3000 30` to broadcast on the LAN.)
4. Port 3000 is decoded as DIS automatically. On another port: right click a packet → Decode As… → DIS.
   Each packet should show "Entity State" with entity 1:1:n, marking "GODOT n" and the location.

From the command line (capture 12 s on loopback while the sender runs, then print the packets):
```powershell
& "C:\Program Files\Wireshark\tshark.exe" -i \Device\NPF_Loopback -f "udp port 3000" -a duration:12 -w dis.pcapng
& "C:\Program Files\Wireshark\tshark.exe" -r dis.pcapng            # one line per PDU
& "C:\Program Files\Wireshark\tshark.exe" -r dis.pcapng -c 1 -V -O dis   # every field of the first PDU
```
