extends SceneTree
## Sends a few moving Entity State PDUs for a while, e.g. to watch them in Wireshark.
## <godot> --headless --path . -s res://tools/send_entities.gd -- [address] [port] [seconds]
## Defaults: 127.0.0.1 3000 30. Entities circle around Ankara at 1 Hz updates.

const ANKARA_ECEF := [4118639.58, 2654282.08, 4069473.68]

var _network: DisNetwork
var _elapsed := 0.0
var _next_send := 0.0
var _duration := 30.0


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	_network = DisNetwork.new()
	_network.mode = DisNetwork.MODE_BROADCAST if args.size() > 0 and args[0].ends_with(".255") \
			else DisNetwork.MODE_UNICAST
	_network.destination_address = args[0] if args.size() > 0 else "127.0.0.1"
	_network.destination_port = int(args[1]) if args.size() > 1 else 3000
	_network.port = 0 if _network.destination_port != 3000 else 3001
	_duration = float(args[2]) if args.size() > 2 else 30.0
	root.add_child(_network)
	print("sending to %s:%d for %d s" % [_network.destination_address, _network.destination_port, _duration])


func _process(delta: float) -> bool:
	_elapsed += delta
	if _elapsed >= _next_send:
		_next_send += 1.0
		for n in range(1, 4):
			var angle := _elapsed * 0.1 + n * TAU / 3.0
			var pdu := DisEntityStatePdu.new()
			pdu.entity_id = Vector3i(1, 1, n)
			pdu.force_id = 1
			pdu.entity_type = PackedInt32Array([1, 1, 225, 1, 1, 0, 0])
			pdu.marking = "GODOT %d" % n
			pdu.location_ecef = PackedFloat64Array([ANKARA_ECEF[0] + 500.0 * cos(angle),
					ANKARA_ECEF[1] + 500.0 * sin(angle), ANKARA_ECEF[2]])
			pdu.timestamp = DisPdu.timestamp_from_seconds(fmod(Time.get_unix_time_from_system(), 3600.0), true)
			_network.send_pdu(pdu)
	return _elapsed >= _duration
