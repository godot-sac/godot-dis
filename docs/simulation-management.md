# Simulation management: starting, stopping and removing

DIS exercises are started, frozen and resumed by a *simulation manager* (SIMAN) with Simulation Management PDUs
(protocol family 5); every application answers each request with an Acknowledge PDU. godot-dis implements the four
PDUs exercise control needs and a node that plays both sides.

| PDU | Type | Size | Classes |
|---|---:|---:|---|
| Create Entity | 11 | 28 | `DisCreateEntityPdu` |
| Remove Entity | 12 | 28 | `DisRemoveEntityPdu` |
| Start/Resume | 13 | 44 | `DisStartResumePdu` |
| Stop/Freeze | 14 | 40 | `DisStopFreezePdu` |
| Acknowledge | 15 | 32 | `DisAcknowledgePdu` |

All four derive from `DisSimulationManagementPdu` (and that from `DisPduBase`, like every PDU class):
`originating_id`, `receiving_id`, `request_id`, and `is_addressed_to(site, application)`.

## DisExerciseControl

```gdscript
# Scene: DisNetwork, DisExerciseControl (and your DisEntity nodes, DisEntityManager, ...)
@onready var exercise: DisExerciseControl = $DisExerciseControl

func _ready():
    exercise.stopped.connect(func(pdu): print("frozen by ", pdu.originating_id if pdu else "us"))
    exercise.started.connect(func(pdu): print("running, simulation time ", exercise.get_simulation_time()))
```

**As a managed application** (every simulation): a Start/Resume or Stop/Freeze whose receiving ID names this
application (site and application of the `DisNetwork`, or 0xFFFF for "all") changes `state`
(`STATE_RUNNING` / `STATE_STOPPED`) and emits `started(pdu)` / `stopped(pdu)` and `state_changed(state)`.

- The request is acknowledged at once ("able to comply") when `auto_acknowledge` is on (default).
- If the request names a real world time in the future, the change waits for it (`get_scheduled_time()`); a time
  more than a day ahead is taken as a clock mismatch and applied at once. A time of 0 means "now".
- A request sent again (same originator and request ID, because our answer got lost) is acknowledged again but
  carried out only once.
- `get_simulation_time()`: set by Start/Resume, runs while running, or while stopped if the Stop/Freeze set
  `FROZEN_RUN_SIMULATION_CLOCK`. `get_frozen_behavior()` gives the other bits (`FROZEN_TRANSMIT_UPDATES`,
  `FROZEN_PROCESS_UPDATES`).
- `pause_tree`: stopping pauses the scene tree (`get_tree().paused`). The control node and its network are switched
  to `PROCESS_MODE_ALWAYS`, so a Start/Resume can still arrive; nodes that should keep running while frozen (UI)
  need `PROCESS_MODE_ALWAYS` too. Paused `DisEntity` nodes don't publish, a paused `DisEntityManager` doesn't
  dead-reckon (both match "frozen" with no behavior bits).
- `start_stopped`: begin in `STATE_STOPPED` and wait for a Start/Resume.
- A Remove Entity request for one of this application's `DisEntity` nodes (or 0xFFFF = all of them) emits
  `remove_entity_requested(entity_id, pdu)`; with `remove_entities` (default) the `DisEntity` node is freed, which
  sends its final "deactivated" Entity State PDU. The answer is "able to comply", or "unable to comply" if there
  is no such entity.
- A Create Entity request emits `create_entity_requested(pdu)`. The game decides what to create, so the answer is
  "able to comply" if something is connected to that signal and "unable to comply" otherwise.
- `start()` / `stop(frozen_behavior)` change the state by hand; `handle_pdu(pdu)` applies a PDU without the network.

**As a simulation manager:**

```gdscript
var id := exercise.send_stop_freeze()                       # everyone, reason Recess, now
exercise.send_start_resume(Vector3i(2, 1, 0), Time.get_unix_time_from_system() + 5.0)  # site 2 app 1, in 5 s
exercise.send_remove_entity(Vector3i(2, 1, 7))              # ask the owner of 2:1:7 to remove it
exercise.acknowledged.connect(func(request_id, response, pdu): print(request_id, " answered by ", pdu.originating_id))
exercise.request_failed.connect(func(request_id): print(request_id, " got no answer"))
```

A request to one application is sent again every `retry_interval` (1 s) until it is acknowledged or `max_retries`
(3) resends went unanswered (`request_failed`). A request to everyone is sent once; each answer arrives as
`acknowledged`, and `request_failed` fires only if nobody answered within `retry_interval × (max_retries + 1)`.
`get_pending_requests()` lists what is still open. Our own PDUs don't loop back to us (`ignore_own_packets`), so a
manager that wants to stop itself too calls `stop()` as well — the demo does.

## Comment, Set Data and Data

Three more PDUs of the family carry *datum records* instead of commands:

| PDU | Type | Class | What for |
|---|---:|---|---|
| Set Data | 19 | `DisSetDataPdu` | ask the receiver to set the given values (with a request ID) |
| Data | 20 | `DisDataPdu` | values, as an answer or unsolicited (with a request ID) |
| Comment | 22 | `DisCommentPdu` | free text and other data for loggers and operators (no request ID) |

All three derive from `DisDatumPdu`: `fixed_datums` (Array of `{id, value}`, 32-bit ID and 32 raw bits) and
`variable_datums` (Array of `{id, length, data}`, the length in bits and the data as `PackedByteArray`; on the wire
each is padded to a multiple of 8 bytes). Datum IDs are SISO-REF-010 "Variable Record Types" or agreed within the
exercise. Helpers: `add_fixed_datum(id, value)`, `add_variable_datum(id, data, length_bits = -1)`,
`add_text(id, text)` (UTF-8) and `get_text(id)`.

```gdscript
var comment := DisCommentPdu.new()
comment.originating_id = Vector3i(network.site_id, network.application_id, 0)
comment.receiving_id = Vector3i(0xFFFF, 0xFFFF, 0xFFFF)
comment.add_text(1, "Phase line ALPHA crossed")
network.send_pdu(comment)

network.comment_received.connect(func(pdu: DisCommentPdu): print(pdu.get_text(1)))
```

They are encoded with godot-dis's own code: open-dis-cpp's variable datum rounds bit lengths down to whole bytes.
DisExerciseControl does not answer Set Data or Data Query; connect to `set_data_received` for that.

## Byte layouts (DIS 7, big-endian)

Taken from open-dis-cpp's DIS 7 classes and checked byte for byte against Open-DIS Python (`tools/golden_pdus.py`)
and opendis7-java (its AllPduSender capture, `tests/unit/test_captures.gd`). The IEEE text is not reproduced.

Every PDU starts with the 12-byte header (see [entity-state-pdu.md](entity-state-pdu.md)) and then:

| Offset | Size | Field |
|---:|---:|---|
| 12 | 6 | Originating entity ID: site, application, entity (3 × uint16) |
| 18 | 6 | Receiving entity ID: site, application, entity |

| PDU | Offset | Size | Field |
|---|---:|---:|---|
| Create Entity, Remove Entity | 24 | 4 | Request ID (uint32) |
| Start/Resume | 24 | 8 | Real world time: hour (int32), time past the hour (uint32, timestamp units) |
| | 32 | 8 | Simulation time: same layout |
| | 40 | 4 | Request ID |
| Stop/Freeze | 24 | 8 | Real world time |
| | 32 | 1 | Reason (SISO-REF-010 UID 67: 0 other, 1 recess, 2 termination, ...) |
| | 33 | 1 | Frozen behavior (UID 68 bits: run simulation clock, transmit updates, process updates) |
| | 34 | 2 | Padding |
| | 36 | 4 | Request ID |
| Acknowledge | 24 | 2 | Acknowledge flag (UID 69: 1 create, 2 remove, 3 start/resume, 4 stop/freeze, 5 transfer) |
| | 26 | 2 | Response flag (UID 70: 0 other, 1 able, 2 unable, 3 pending operator action, 4 more info) |
| | 28 | 4 | Request ID |
| Set Data, Data | 24 | 4 | Request ID |
| | 28 | 4 | Padding |
| | 32 | 4 | Number of fixed datum records F |
| | 36 | 4 | Number of variable datum records V |
| | 40 | 8 × F | Fixed datums: ID (uint32), value (32 bits) |
| | | | Variable datums: ID (uint32), length in bits (uint32), data padded to a multiple of 8 bytes |
| Comment | 24 | 4 | Number of fixed datum records |
| | 28 | 4 | Number of variable datum records |
| | 32 | | Fixed, then variable datums as above |

Clock time: the hour counts hours since 1970-01-01 UTC for real world time (`set_real_world_time(unix_seconds)`)
and hours since the start for simulation time (`set_simulation_time(seconds)`); the second field counts
3600 s / 2^31 units past the hour, shifted left by one, with the low bit 1 for absolute time — the same encoding as
the header timestamp. The raw fields (`real_world_time_hour`, `real_world_time_past_hour`, ...) round-trip exactly.

Names for all enumeration values: `DisEnums.get_enum_name("stop_freeze_reason" | "acknowledge_flag" |
"acknowledge_response", value)`.

## Interoperability

The demo's civilian traffic (`tools/demo_traffic.py`) is an Open-DIS Python application that obeys Stop/Freeze,
Start/Resume and Remove Entity and answers with Open-DIS Acknowledge PDUs, so the exercise demo shows a Godot
manager controlling a non-Godot application. Details and the other checks: [interoperability.md](interoperability.md).
