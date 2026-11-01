extends SceneTree
## godot-dis command line tool: record, play back, inspect and convert DIS recordings (pcapng, pcap, .pdulog).
##
##   <godot> --headless --path <project> -s res://addons/dis/tools/dis_cli.gd -- <command> [options]
## Relative file names are relative to the project folder.
##
## Commands:
##   record <file>           record DIS traffic until --seconds pass (or Ctrl+C; the file is flushed every second)
##   play <file>             send a recording onto the network again, in real time (--speed to change)
##   info <file>             duration, packets, PDU types, exercises, sites, entities
##   dump <file>             one line per PDU
##   convert <in> <out>      between pcapng / pcap (read only) / .pdulog, by extension
## Network options (record, play): --mode broadcast|multicast|unicast (default broadcast), --port 3000,
##   --group 239.1.2.3 (multicast), --destination <address> (play: broadcast / unicast target), --interface <name or IP>
## record: --seconds N (0 = until stopped)   play: --speed X, --loop, --exercise N (rewrite the exercise ID),
##   --start S (begin S seconds in)   dump: --limit N   info / dump: --port N (only that UDP port of a capture)
## See docs/recording.md.

const MODES := {"broadcast": DisNetwork.MODE_BROADCAST, "unicast": DisNetwork.MODE_UNICAST,
		"multicast": DisNetwork.MODE_MULTICAST}

var _args: PackedStringArray
var _network: DisNetwork
var _recorder: DisRecorder
var _player: DisPlayer
var _seconds := 0.0
var _elapsed := 0.0
var _report_clock := 0.0
var _exit_code := 0
var _started := false


func _initialize() -> void:
	_args = OS.get_cmdline_user_args()


## Runs the command on the first frame, when the scene tree is up (nodes can find each other).
func _run() -> void:
	if _args.is_empty() or _args[0] in ["help", "-h", "--help"]:
		_usage()
		_finish(0 if not _args.is_empty() else 1)
		return
	var command := _args[0]
	var file := _args[1] if _args.size() > 1 and not _args[1].begins_with("--") else ""
	match command:
		"record":
			_record(file)
		"play":
			_play(file)
		"info":
			_info(file)
		"dump":
			_dump(file)
		"convert":
			_convert(file, _args[2] if _args.size() > 2 else "")
		_:
			printerr("unknown command '%s'" % command)
			_usage()
			_finish(1)


func _option(name: String, default: String) -> String:
	var i := _args.find(name)
	return _args[i + 1] if i >= 0 and i + 1 < _args.size() else default


func _flag(name: String) -> bool:
	return _args.has(name)


func _usage() -> void:
	print("usage: <godot> --headless --path <project> -s res://addons/dis/tools/dis_cli.gd -- <command> [options]")
	print("  record <file> [--seconds N] [--mode broadcast|multicast|unicast] [--port 3000] [--group 239.1.2.3]")
	print("  play <file> [--speed 1] [--loop] [--start S] [--exercise N] [--mode ...] [--port] [--group] [--destination]")
	print("  info <file> [--port N]")
	print("  dump <file> [--limit N] [--port N]")
	print("  convert <in> <out>   (formats by extension: .pcapng, .pdulog; .pcap is read only)")


func _finish(code: int) -> void:
	_exit_code = code
	quit(code)


## Command-line file names: absolute, res://, user://, or relative to the project folder (Godot runs there).
func _path(file: String) -> String:
	if file.begins_with("res://") or file.begins_with("user://") or file.is_absolute_path():
		return file
	return ProjectSettings.globalize_path("res://").path_join(file)


func _make_network() -> DisNetwork:
	var network := DisNetwork.new()
	network.mode = MODES.get(_option("--mode", "broadcast"), DisNetwork.MODE_BROADCAST)
	network.port = int(_option("--port", "3000"))
	network.multicast_group = _option("--group", "239.1.2.3")
	network.multicast_interface = _option("--interface", "")
	if _option("--destination", "") != "":
		network.destination_address = _option("--destination", "")
	network.filter_exercise = false
	network.ignore_own_packets = false
	network.site_id = 0xFFFE  # a tool, not a simulation: no real PDU carries this site
	network.autostart = false
	root.add_child(network)
	network.start()
	return network


func _describe_network(network: DisNetwork) -> String:
	match network.mode:
		DisNetwork.MODE_MULTICAST:
			return "multicast %s:%d" % [network.multicast_group, network.port]
		DisNetwork.MODE_UNICAST:
			return "unicast port %d -> %s" % [network.port, network.destination_address]
	return "broadcast port %d -> %s" % [network.port, network.destination_address]


# ---------------------------------------------------------------------------------------------------------------

func _record(file: String) -> void:
	if file.is_empty():
		printerr("record: give a file name (.pcapng or .pdulog)")
		_finish(1)
		return
	_seconds = float(_option("--seconds", "0"))
	_network = _make_network()
	if not _network.is_running():
		printerr("record: could not open the network")
		_finish(1)
		return
	_recorder = DisRecorder.new()
	root.add_child(_recorder)
	_recorder.network_path = _recorder.get_path_to(_network)
	if _recorder.start(_path(file)) != OK:
		_finish(1)
		return
	print("recording %s to %s%s" % [_describe_network(_network), _recorder.get_recording_path(),
			" for %s s" % _seconds if _seconds > 0 else " (Ctrl+C to stop)"])


func _play(file: String) -> void:
	var recording := _load(file)
	if recording == null:
		return
	_network = _make_network()
	if not _network.is_running():
		printerr("play: could not open the network")
		_finish(1)
		return
	_player = DisPlayer.new()
	_player.output = DisPlayer.OUTPUT_NETWORK
	_player.speed = float(_option("--speed", "1"))
	_player.loop = _flag("--loop")
	_player.exercise_id_override = int(_option("--exercise", "-1"))
	root.add_child(_player)
	_player.network_path = _player.get_path_to(_network)
	_player.recording = recording
	_player.finished.connect(func():
		print("finished: %d packets sent" % _network.get_statistics().sent)
		_finish(0))
	_player.seek(float(_option("--start", "0")))
	_player.play()
	print("playing %s (%d packets, %.1f s) at %sx to %s%s" % [file, recording.get_packet_count(),
			recording.get_duration(), _player.speed, _describe_network(_network), ", looping" if _player.loop else ""])


func _process(delta: float) -> bool:
	if not _started:
		_started = true
		_run()
		return false
	_elapsed += delta
	_report_clock += delta
	if _recorder != null and _recorder.is_recording():
		if _report_clock >= 1.0:
			_report_clock = 0.0
			print("  %4d s  %d packets" % [roundi(_recorder.get_elapsed()), _recorder.get_packet_count()])
		if _seconds > 0.0 and _recorder.get_elapsed() >= _seconds:
			_recorder.stop()
			print("recorded %d packets to %s" % [_recorder.get_packet_count(), _recorder.get_recording_path()])
			_finish(0)
	if _player != null and _player.is_playing() and _report_clock >= 1.0:
		_report_clock = 0.0
		print("  %6.1f / %.1f s  %d sent" % [_player.get_position(), _player.get_duration(),
				_network.get_statistics().sent])
	return false


# ---------------------------------------------------------------------------------------------------------------

func _load(file: String) -> DisRecording:
	if file.is_empty():
		printerr("give a recording file")
		_finish(1)
		return null
	var recording := DisRecording.new()
	if recording.load(_path(file), int(_option("--port", "0")) if _args[0] != "play" else 0) != OK:
		printerr("cannot read %s: %s" % [file, recording.get_last_error()])
		_finish(1)
		return null
	if recording.get_last_error() != "":
		printerr("warning: %s (%d packets read before it)" % [recording.get_last_error(), recording.get_packet_count()])
	return recording


static func _type_name(type: int) -> String:
	var name := DisEnums.get_enum_name("pdu_type", type)
	return name if name != "" else "type %d" % type


func _info(file: String) -> void:
	var recording := _load(file)
	if recording == null:
		return
	var summary := recording.get_summary()
	var stats := recording.get_read_statistics()
	print("file:       %s" % file)
	print("start:      %s UTC" % Time.get_datetime_string_from_unix_time(int(summary.start_time), true))
	print("duration:   %.3f s" % summary.duration)
	print("packets:    %d DIS datagrams (of %d frames; skipped: %d not UDP, %d fragments, %d not DIS, %d other ports, %d cut off)"
			% [summary.packets, stats.frames, stats.not_udp, stats.fragments, stats.not_dis, stats.filtered,
			stats.truncated])
	print("exercises:  %s" % ", ".join(PackedStringArray(summary.exercises.map(func(e): return str(e)))))
	print("sites:      %s" % ", ".join(PackedStringArray(summary.sites)))
	print("entities:   %d (Entity State PDUs)" % summary.entities)
	print("PDU types:")
	var types: Array = summary.pdu_types.keys()
	types.sort()
	for type in types:
		print("  %3d  %-28s %d" % [type, _type_name(type), summary.pdu_types[type]])
	_finish(0)


static func _id(v: Vector3i) -> String:
	return "%d:%d:%d" % [v.x, v.y, v.z]


func _dump(file: String) -> void:
	var recording := _load(file)
	if recording == null:
		return
	var limit := int(_option("--limit", "0"))
	var count := recording.get_packet_count() if limit <= 0 else mini(limit, recording.get_packet_count())
	for i in count:
		var packet := recording.get_packet(i)
		var bytes: PackedByteArray = packet.bytes
		var pdu := DisPdu.decode(bytes)
		var detail := ""
		if pdu is DisEntityStatePdu:
			detail = "%s %-11s force %d" % [_id(pdu.entity_id), "'%s'" % pdu.marking, pdu.force_id]
		elif pdu is DisWarfarePdu:
			detail = "%s -> %s event %s" % [_id(pdu.firing_entity_id), _id(pdu.target_entity_id), _id(pdu.event_id)]
		elif pdu is DisSimulationManagementPdu:
			detail = "%s -> %s request %d" % [_id(pdu.originating_id), _id(pdu.receiving_id), pdu.request_id]
		elif pdu == null and DisPdu.is_supported(bytes[2]):
			detail = "malformed: " + DisPdu.get_last_error()
		print("%10.3f  %s:%d  ex %3d  %-22s %4d B  %s" % [packet.time, packet.source_ip, packet.source_port, bytes[1],
				_type_name(bytes[2]), bytes.size(), detail])
	if count < recording.get_packet_count():
		print("... %d more" % (recording.get_packet_count() - count))
	_finish(0)


func _convert(input: String, output: String) -> void:
	if output.is_empty():
		printerr("convert: give an input and an output file")
		_finish(1)
		return
	var recording := _load(input)
	if recording == null:
		return
	var error := recording.save(_path(output))
	if error != OK:
		printerr("cannot write %s: %s" % [output, error_string(error)])
		_finish(1)
		return
	print("wrote %d packets to %s" % [recording.get_packet_count(), output])
	_finish(0)
