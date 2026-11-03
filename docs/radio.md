# Radio: Transmitter and Signal

Radios in DIS are described by the Radio Communications family (protocol family 4): a **Transmitter** PDU for the
state of each radio (on / off / transmitting, frequency, power, modulation, antenna) and **Signal** PDUs that carry
what is transmitted (encoded audio or tactical data link messages). godot-dis encodes and decodes both and tells you
who is on the air; it does not decode, play or record audio.

| PDU | Type | Size | Class |
|---|---:|---:|---|
| Transmitter | 25 | 104 + modulation parameters + antenna pattern + variable records | `DisTransmitterPdu` |
| Signal | 26 | 32 + data, padded to a multiple of 4 bytes | `DisSignalPdu` |

Both are encoded with godot-dis's own small big-endian codec (open-dis-cpp has no DIS 7 Transmitter PDU).

## Who is on the air: DisRadioMonitor

```gdscript
# Scene: DisNetwork, DisRadioMonitor (finds the network by itself, or set network_path)
$DisRadioMonitor.transmission_started.connect(func(entity_id, radio, frequency):
    print("%s transmits on %.3f MHz" % [entity_id, frequency / 1e6]))
$DisRadioMonitor.transmission_ended.connect(func(entity_id, radio): print(entity_id, " stopped"))
...
if $DisRadioMonitor.is_transmitting(remote.entity_id):
    show_radio_icon(remote)
```

A radio is transmitting while its last Transmitter PDU says "on and transmitting" or a Signal PDU from it arrived
less than `signal_hold` (0.5 s) ago — some senders key the radio in the Transmitter PDU, others only send Signals.
Radios not heard from for `timeout` (12 s) are forgotten; at most `max_radios` (1000, 0 = no limit) are tracked, PDUs
of further new radios are dropped (`get_dropped_count()`, one warning). `get_radios()` lists each radio with its entity, number,
frequency, transmit state, power and signal count. Signals: `radio_added`, `radio_removed`, `transmission_started`,
`transmission_ended`.

The exercise demo shows units on the air with pulsing waves on the map: press T for a radio call from the
helicopter; the Open-DIS Python traffic's airliner talks for 3 s every 20 s.

## Sending

```gdscript
var transmitter := DisTransmitterPdu.new()
transmitter.radio_reference_id = $Helicopter/DisEntity.get_effective_entity_id()
transmitter.radio_number = 1
transmitter.radio_type = PackedInt32Array([7, 2, 0, 1, 1, 0])   # kind, domain, country, category, version, nomenclature
transmitter.transmit_state = DisTransmitterPdu.TRANSMIT_ON_AND_TRANSMITTING
transmitter.antenna_location_ecef = origin.local_to_ecef($Helicopter.global_position)
transmitter.frequency = 243000000   # Hz
transmitter.bandwidth = 25000.0     # Hz
transmitter.power = 40.0            # dBm
network.send_pdu(transmitter)

var block := DisSignalPdu.new()
block.radio_reference_id = transmitter.radio_reference_id
block.radio_number = 1
block.encoding_scheme = 4           # encoded audio (class 0), 16-bit linear PCM
block.sample_rate = 8000
block.samples = 400
block.set_data_bytes(pcm_bytes)     # sets data and data_length (in bits)
network.send_pdu(block)
```

Keep Signal PDUs under the network's MTU (about 1400 bytes of data on Ethernet): larger datagrams are fragmented,
and many receivers (and our capture reader) drop fragments.

## Fields

`DisTransmitterPdu`: `radio_reference_id` (the entity the radio belongs to), `radio_number`, `radio_type` (6 ints:
kind, domain, country, category, nomenclature version, nomenclature), `transmit_state` (0 off, 1 on, 2 on and
transmitting), `input_source`, `antenna_location_ecef` (3 doubles), `relative_antenna_location` (body frame),
`antenna_pattern_type`, `frequency` (Hz, 64-bit), `bandwidth` (Hz), `power` (dBm), `modulation_type` (4 ints: spread
spectrum bits, major modulation, detail, radio system), `crypto_system`, `crypto_key_id`, and the variable parts as
raw bytes: `modulation_parameters`, `antenna_pattern`, `variable_parameters` (an Array of whole records: type (4),
length (2), data).

`DisSignalPdu`: `radio_reference_id`, `radio_number`, `encoding_scheme` (bits 14–15 class, 0–13 type), `tdl_type`,
`sample_rate`, `data_length` (bits), `samples`, `data` (ceil(data_length / 8) bytes).

## Byte layouts (DIS 7, big-endian)

Transmitter, after the 12-byte header:

| Offset | Size | Field |
|---:|---:|---|
| 12 | 6 | Radio reference ID (site, application, entity) |
| 18 | 2 | Radio number |
| 20 | 8 | Radio type: kind, domain, country (2), category, nomenclature version, nomenclature (2) |
| 28 | 1 | Transmit state |
| 29 | 1 | Input source |
| 30 | 2 | Number of variable transmitter parameter records |
| 32 | 24 | Antenna location (3 × float64, ECEF) |
| 56 | 12 | Relative antenna location (3 × float32) |
| 68 | 2 | Antenna pattern type |
| 70 | 2 | Antenna pattern length (bytes) |
| 72 | 8 | Frequency (uint64, Hz) |
| 80 | 4 | Transmit frequency bandwidth (float32, Hz) |
| 84 | 4 | Power (float32, dBm) |
| 88 | 8 | Modulation type: spread spectrum, major modulation, detail, radio system (4 × uint16) |
| 96 | 2 | Crypto system |
| 98 | 2 | Crypto key ID |
| 100 | 1 | Modulation parameters length (bytes) |
| 101 | 3 | Padding |
| 104 | | Modulation parameters, then the antenna pattern, then the variable transmitter parameter records |

Signal, after the header:

| Offset | Size | Field |
|---:|---:|---|
| 12 | 6 | Radio reference ID |
| 18 | 2 | Radio number |
| 20 | 2 | Encoding scheme |
| 22 | 2 | TDL type |
| 24 | 4 | Sample rate |
| 28 | 2 | Data length (bits) |
| 30 | 2 | Number of samples |
| 32 | | Data, padded with zeros to a multiple of 4 bytes |

## Interoperability notes
- Checked byte for byte against Open-DIS Python (golden PDUs, both directions over UDP), the Wireshark sample
  captures (558 + 243 Signal and 10 Transmitter PDUs, from a DIS 5 sender) and opendis7-java's capture.
- The DIS 5 sender in the Wireshark samples writes protocol family 0 in the header and pads its Transmitter PDUs
  with 45 extra zero bytes; godot-dis keeps both (`trailing_bytes`, the received family) so the PDUs go back out
  unchanged.
- Open-DIS Python does not pad Signal data to 32 bits; godot-dis does, as the layout above asks.
- Wireshark 4.6 reports a Signal PDU with no data at all (data length 0) as malformed.
