extends Node
## godot-dis demo: one simulation of a two-sided exercise around Ankara.
##
## Run two copies (and optionally tools/demo_traffic.py) on one computer or on a LAN:
##   <godot> --path . -- --side blue
##   <godot> --path . -- --side red
## Each copy publishes its own units with DisEntity and shows every unit on the network (its own and the others')
## on a tactical map. Everyone uses multicast 239.1.2.3:3000, so copies on one computer share the port.
## Keys: F fire at the nearest enemy, X remove the remote unit under the mouse (Remove Entity request),
## S stop / freeze the exercise, R start / resume it, T a radio call from the helicopter (Transmitter + Signal). --record <file.pcapng|.pdulog> records the session for
## demo/replay.tscn. See docs/demo.md.

## Damage bits of the DIS general appearance (bits 3-4): 3 = destroyed.
const APPEARANCE_DESTROYED := 3 << 3
const NEUTRAL := 3
const MAX_RANGE := 30000.0  # metres; the sides start 10-40 km apart
const HIT_CHANCE := 0.7
const MAX_FLIGHT := 4.0  # seconds; shortened from the real time of flight so the demo stays lively
# Munition entity types (SISO-REF-010 kind 2): domain anti-armor / anti-air, category ballistic / guided.
const TANK_ROUND := [2, 2, 0, 2, 0, 0, 0]
const AIR_MISSILE := [2, 1, 0, 1, 0, 0, 0]
const EVENT_LINES := 12
const RADIO_FREQUENCY := 243000000  # Hz
const RADIO_TYPE := [7, 2, 0, 1, 1, 0]  # radio, air, generic, voice transmission / reception
const CALL_SECONDS := 2.0
const COMMENT_TEXT_ID := 1

@onready var network: DisNetwork = $DisNetwork
@onready var origin: DisGeoOrigin = $DisGeoOrigin
@onready var manager: DisEntityManager = $DisEntityManager
@onready var exercise: DisExerciseControl = $DisExerciseControl
@onready var radios: DisRadioMonitor = $DisRadioMonitor
@onready var own_forces: Node3D = $OwnForces

var side := "blue"
var _map: DemoTacticalMap
var _effects: DemoMapEffects
var _status: Label
var _list: RichTextLabel
var _log: RichTextLabel
var _events: PackedStringArray = []
var _type_cache := {}  # "k:d:c:..." + force + status -> {name, sidc}
var _panel_clock := 0.0
var _rng := RandomNumberGenerator.new()
var _shots: Array = []  # own rounds in flight: {fire, shooter, target, aim, arrive, hit}
var _next_shooter := 0
var _received_types := {}  # PDU type -> count
var _recorder: DisRecorder
var _call_left := 0.0  # seconds left of our radio call
var _call_clock := 0.0
var _call_unit: DemoUnitMover
var _sent_types := {}
# Optional: --screenshot <file.png> saves the window after --seconds; --seconds alone quits after that time.
var _screenshot := ""
var _quit_after := 0.0
var _started := 0.0
# Optional scripted actions for unattended runs: --auto-fire <interval s>, --stop-at <s>, --resume-at <s>,
# --remove <site:application:entity>@<s>, --radio-at <s>.
var _auto_fire := 0.0
var _auto_fire_clock := 0.0
var _stop_at := -1.0
var _resume_at := -1.0
var _remove_id := Vector3i()
var _remove_at := -1.0
var _radio_at := -1.0
# Optional: --capture <dir> saves the window as PNG frames (dir/f00000000.png ...) at CAPTURE_FPS of real time, for
# tools/make_media.py. Real time on purpose: Godot's Movie Maker mode slows the frame clock against the wall clock
# that DIS timestamps, dead reckoning and the other simulations run on.
const CAPTURE_FPS := 10.0
var _capture_dir := ""
var _capture_frame := 0
var _capture_tasks := PackedInt64Array()


func _ready() -> void:
	side = _argument("--side", "blue")
	if not DemoScenario.SIDES.has(side):
		push_warning("unknown side '%s', using blue" % side)
		side = "blue"
	var info: Dictionary = DemoScenario.SIDES[side]
	network.site_id = info.site
	_rng.seed = hash(side)
	var port := int(_argument("--port", str(network.port)))
	if port != network.port:  # --port: a separate exercise, e.g. for tests next to a running demo
		network.stop()
		network.port = port
		network.start()
	get_window().title = "godot-dis demo — %s" % info.title
	_build_ui(info.title)
	_spawn_units(info)
	_connect_signals()
	_screenshot = _argument("--screenshot", "")
	var record := _argument("--record", "")
	if record != "":  # everything this simulation sends and receives, for demo/replay.tscn or Wireshark
		_recorder = DisRecorder.new()
		add_child(_recorder)
		_recorder.network_path = _recorder.get_path_to(network)
		if _recorder.start(record) == OK:
			_event("Recording to %s" % record)  # as given (user://...), no local path on screen
	_quit_after = float(_argument("--seconds", "0"))
	_capture_dir = _argument("--capture", "")
	if _capture_dir != "":
		DirAccess.make_dir_recursive_absolute(_capture_dir)
	_auto_fire = float(_argument("--auto-fire", "0"))
	_stop_at = float(_argument("--stop-at", "-1"))
	_resume_at = float(_argument("--resume-at", "-1"))
	_radio_at = float(_argument("--radio-at", "-1"))
	var remove := _argument("--remove", "").split("@")
	if remove.size() == 2:
		var parts := remove[0].split(":")
		_remove_id = Vector3i(int(parts[0]), int(parts[1]), int(parts[2]))
		_remove_at = float(remove[1])
	_started = Time.get_ticks_msec() / 1000.0
	_event("Ready: site %d, %d own units. F fire, X remove, S stop, R resume." % [network.site_id,
			own_forces.get_child_count()])


func _argument(name: String, default: String) -> String:
	var args := OS.get_cmdline_user_args()
	var i := args.find(name)
	return args[i + 1] if i >= 0 and i + 1 < args.size() else default


func _spawn_units(info: Dictionary) -> void:
	var units := DemoScenario.units(side)
	for i in units.size():
		var unit: Dictionary = units[i]
		var mover := DemoUnitMover.new()
		mover.name = unit.marking
		mover.route = unit.route
		mover.speed = unit.speed
		mover.altitude = unit.altitude
		mover.distance = unit.start
		var entity := DisEntity.new()
		entity.entity_id = Vector3i(0, 0, i + 1)  # site / application from the network
		entity.force_id = info.force
		entity.entity_type = unit.entity_type
		entity.marking = unit.marking
		entity.dr_algorithm = unit.dr_algorithm
		mover.add_child(entity)
		own_forces.add_child(mover)


func _connect_signals() -> void:
	network.pdu_received.connect(func(pdu: DisPduBase, _ip, _port):
		_received_types[pdu.get_pdu_type()] = _received_types.get(pdu.get_pdu_type(), 0) + 1)
	network.fire_received.connect(_on_fire)
	network.detonation_received.connect(_on_detonation)
	exercise.started.connect(_on_started)
	exercise.stopped.connect(_on_stopped)
	exercise.acknowledged.connect(_on_acknowledged)
	exercise.request_failed.connect(func(id): _event("[color=#f99]No answer to request #%d[/color]" % id))
	exercise.remove_entity_requested.connect(_on_remove_requested)
	network.comment_received.connect(func(pdu: DisCommentPdu): _event("[color=#cce]COMMENT[/color] %s: %s" % [
			_id_text(pdu.originating_id), pdu.get_text(COMMENT_TEXT_ID)]))
	radios.transmission_started.connect(func(id, radio, frequency): _event("[color=#adf]RADIO[/color] %s on %.3f MHz" % [
			_unit_name(id), frequency / 1.0e6]))
	network.collision_received.connect(func(pdu: DisCollisionPdu): _event("COLLISION %s with %s" % [
			_unit_name(pdu.issuing_entity_id), _unit_name(pdu.colliding_entity_id)]))


# ---------------------------------------------------------------------------------------------------------------
# Units

func _type_info(entity_type: PackedInt32Array, force: int, status := 0) -> Dictionary:
	var key := "%s/%d/%d" % [entity_type, force, status]
	if not _type_cache.has(key):
		_type_cache[key] = {"name": DisEnums.get_entity_type_name(entity_type),
				"sidc": DisEnums.entity_type_to_sidc(entity_type, force, status)}
	return _type_cache[key]


static func _heading(basis: Basis) -> float:
	var forward := -basis.z
	return atan2(forward.x, -forward.z)  # clockwise from north (-Z)


static func _id_text(id: Vector3i) -> String:
	return "%d:%d:%d" % [id.x, id.y, id.z]


func _own_entity(mover: Node) -> DisEntity:
	return mover.get_child(0) as DisEntity


func _collect_units() -> Array:
	var result := []
	for mover: DemoUnitMover in own_forces.get_children():
		if mover.is_queued_for_deletion():
			continue
		var entity := _own_entity(mover)
		var status := DisEnums.appearance_to_status(entity.appearance)
		var info := _type_info(entity.entity_type, entity.force_id, status)
		result.append({"key": "own/" + entity.marking, "marking": entity.marking, "name": info.name,
				"sidc": info.sidc, "force": entity.force_id, "position": mover.global_position,
				"heading": _heading(mover.global_basis), "speed": mover.speed,
				"source": "own " + _id_text(entity.get_effective_entity_id()),
				"published": entity.get_published_count(), "destroyed": status == 4,
				"transmitting": _call_left > 0.0 and mover == _call_unit})
	for remote: DisRemoteEntity in manager.get_entities():
		var pdu := remote.get_last_pdu()
		var status := DisEnums.appearance_to_status(remote.get_appearance())
		var info := _type_info(remote.entity_type, remote.force_id, status)
		var velocity := origin.ecef_vector_to_local(pdu.linear_velocity)
		result.append({"key": "remote/%s" % remote.entity_id, "marking": remote.marking, "name": info.name,
				"sidc": info.sidc, "force": remote.force_id, "position": remote.global_position,
				"heading": _heading(remote.global_basis), "speed": Vector2(velocity.x, velocity.z).length(),
				"source": _id_text(remote.entity_id), "published": -1, "destroyed": status == 4,
				"transmitting": radios.is_transmitting(remote.entity_id)})
	return result


func _unit_name(id: Vector3i) -> String:
	for mover in own_forces.get_children():
		if _own_entity(mover).get_effective_entity_id() == id:
			return mover.name
	var remote := manager.get_entity(id) as DisRemoteEntity
	return remote.marking if remote != null else _id_text(id)


# ---------------------------------------------------------------------------------------------------------------
# Weapons

## Fires from the next own unit that has an enemy within MAX_RANGE.
func _fire_once() -> void:
	var own_force: int = DemoScenario.SIDES[side].force
	var shooters := own_forces.get_children().filter(func(m): return (not m.is_queued_for_deletion()
			and DisEnums.appearance_to_status(_own_entity(m).appearance) != 4))
	var enemies := manager.get_entities().filter(func(r: DisRemoteEntity): return (r.force_id != NEUTRAL
			and r.force_id != own_force and DisEnums.appearance_to_status(r.get_appearance()) != 4))
	if shooters.is_empty() or enemies.is_empty():
		_event("No shooter or no enemy to fire at")
		return
	for i in shooters.size():
		var mover: DemoUnitMover = shooters[(_next_shooter + i) % shooters.size()]
		var target: DisRemoteEntity = null
		var best := MAX_RANGE
		for enemy: DisRemoteEntity in enemies:
			var d := mover.global_position.distance_to(enemy.global_position)
			if d < best:
				best = d
				target = enemy
		if target == null:
			continue
		_next_shooter = (_next_shooter + i + 1) % shooters.size()
		var entity := _own_entity(mover)
		var air := entity.entity_type[1] == 2 or target.entity_type[1] == 2
		var speed := 800.0 if air else 1500.0
		var direction := (target.global_position - mover.global_position).normalized()
		var fire := entity.fire(PackedInt32Array(AIR_MISSILE if air else TANK_ROUND), target.entity_id,
				direction * speed, best)
		_count_sent(fire)
		var flight := clampf(best / speed, 0.5, MAX_FLIGHT)
		_shots.append({"fire": fire, "shooter": entity, "target": target.entity_id, "aim": target.global_position,
				"arrive": Time.get_ticks_msec() / 1000.0 + flight, "hit": _rng.randf() < HIT_CHANCE})
		_effects.add_fire(mover.global_position, target.global_position, DemoTacticalMap.color_of(entity.force_id),
				flight)
		_event("[color=#fd8]FIRE[/color] %s → %s, %.1f km (event %d)" % [mover.name, target.marking, best / 1000.0,
				fire.event_id.z])
		return
	_event("No enemy within %d km" % roundi(MAX_RANGE / 1000.0))


func _process_shots() -> void:
	var now := Time.get_ticks_msec() / 1000.0
	var landed := _shots.filter(func(s): return now >= s.arrive)
	_shots = _shots.filter(func(s): return now < s.arrive)
	for shot in landed:
		var shooter: DisEntity = shot.shooter
		if not is_instance_valid(shooter) or shooter.is_queued_for_deletion():
			continue
		var target := manager.get_entity(shot.target) as DisRemoteEntity
		var at: Vector3 = target.global_position if target != null else shot.aim
		var result := DisDetonationPdu.RESULT_ENTITY_IMPACT
		if not shot.hit or target == null:
			at += Vector3(_rng.randf_range(-150, 150), 0, _rng.randf_range(-150, 150))
			result = DisDetonationPdu.RESULT_GROUND_IMPACT
		var detonation := shooter.detonate(at, result, shot.fire)
		_count_sent(detonation)
		_show_detonation(detonation, at)


func _count_sent(pdu: DisPduBase) -> void:
	if pdu != null:
		_sent_types[pdu.get_pdu_type()] = _sent_types.get(pdu.get_pdu_type(), 0) + 1


func _show_detonation(pdu: DisDetonationPdu, at: Vector3) -> void:
	var hit := pdu.detonation_result == DisDetonationPdu.RESULT_ENTITY_IMPACT
	_effects.add_burst(at, Color(1, 0.6, 0.3) if hit else Color(0.8, 0.8, 0.6), hit)
	_event("[color=#f96]DETONATION[/color] %s, event %d: %s" % [DisEnums.get_enum_name("detonation_result",
			pdu.detonation_result), pdu.event_id.z, _unit_name(pdu.target_entity_id) if hit else "miss"])


func _on_fire(pdu: DisFirePdu) -> void:
	var from := origin.ecef_to_local(pdu.location_ecef)
	var target := manager.get_entity(pdu.target_entity_id) as DisRemoteEntity
	var to := from + origin.ecef_vector_to_local(pdu.velocity).normalized() * maxf(pdu.range, 500.0)
	for mover in own_forces.get_children():
		if _own_entity(mover).get_effective_entity_id() == pdu.target_entity_id:
			to = mover.global_position
	if target != null:
		to = target.global_position
	var speed := maxf(origin.ecef_vector_to_local(pdu.velocity).length(), 1.0)
	var shooter := manager.get_entity(pdu.firing_entity_id) as DisRemoteEntity
	var color := DemoTacticalMap.color_of(shooter.force_id) if shooter != null else DemoTacticalMap.UNKNOWN_COLOR
	_effects.add_fire(from, to, color, clampf(from.distance_to(to) / speed, 0.5, MAX_FLIGHT))
	_event("[color=#fd8]FIRE[/color] %s → %s (%s)" % [_unit_name(pdu.firing_entity_id), _unit_name(pdu.target_entity_id),
			DisEnums.get_entity_type_name(pdu.munition_type)])


func _on_detonation(pdu: DisDetonationPdu) -> void:
	_show_detonation(pdu, origin.ecef_to_local(pdu.location_ecef))
	if pdu.detonation_result != DisDetonationPdu.RESULT_ENTITY_IMPACT:
		return
	for mover: DemoUnitMover in own_forces.get_children():
		var entity := _own_entity(mover)
		if entity.get_effective_entity_id() == pdu.target_entity_id and DisEnums.appearance_to_status(entity.appearance) != 4:
			entity.appearance = entity.appearance | APPEARANCE_DESTROYED  # sent at once: appearance changes are urgent
			mover.speed = 0.0
			_event("[color=#f66][b]%s destroyed[/b][/color] by %s" % [mover.name, _unit_name(pdu.firing_entity_id)])


# ---------------------------------------------------------------------------------------------------------------
# Exercise control

func _stop_exercise() -> void:
	exercise.send_stop_freeze()  # to everyone; we stop ourselves below (our own PDUs don't come back to us)
	_count_sent_type(DisPdu.PDU_TYPE_STOP_FREEZE)
	exercise.stop()
	_send_comment("Exercise stopped by %s" % side)


func _resume_exercise() -> void:
	exercise.send_start_resume()
	_count_sent_type(DisPdu.PDU_TYPE_START_RESUME)
	exercise.start()
	_send_comment("Exercise resumed by %s" % side)


## A Comment PDU to everyone: free text that every DIS logger shows.
func _send_comment(text: String) -> void:
	var comment := DisCommentPdu.new()
	comment.originating_id = Vector3i(network.site_id, network.application_id, 0)
	comment.receiving_id = Vector3i(0xFFFF, 0xFFFF, 0xFFFF)
	comment.add_text(COMMENT_TEXT_ID, text)
	network.send_pdu(comment)
	_count_sent(comment)


# ---------------------------------------------------------------------------------------------------------------
# Radio

## Starts a radio call from the helicopter (or the first unit left): a Transmitter PDU "on and transmitting", then
## Signal PDUs with (silent) audio every 0.05 s (800 bytes: one Ethernet frame), then a Transmitter PDU
## "on, not transmitting".
func _start_call() -> void:
	if _call_left > 0.0 or own_forces.get_child_count() == 0:
		return
	_call_unit = own_forces.get_node_or_null("HEL-1") as DemoUnitMover
	if _call_unit == null:
		_call_unit = own_forces.get_child(0)
	_call_left = CALL_SECONDS
	_call_clock = 0.0
	_send_transmitter(DisTransmitterPdu.TRANSMIT_ON_AND_TRANSMITTING)
	_event("[color=#adf]RADIO[/color] %s calling on %.3f MHz" % [_call_unit.name, RADIO_FREQUENCY / 1.0e6])


func _process_call(delta: float) -> void:
	if _call_left <= 0.0 or not is_instance_valid(_call_unit):
		return
	_call_left -= delta
	_call_clock += delta
	if _call_clock >= 0.05:
		_call_clock = 0.0
		var block := DisSignalPdu.new()
		block.radio_reference_id = _own_entity(_call_unit).get_effective_entity_id()
		block.radio_number = 1
		block.encoding_scheme = 4  # encoded audio, 16-bit linear PCM
		block.sample_rate = 8000
		block.samples = 400
		var silence := PackedByteArray()
		silence.resize(800)
		block.set_data_bytes(silence)
		network.send_pdu(block)
		_count_sent(block)
	if _call_left <= 0.0:
		_send_transmitter(DisTransmitterPdu.TRANSMIT_ON_NOT_TRANSMITTING)


func _send_transmitter(state: int) -> void:
	var entity := _own_entity(_call_unit)
	var transmitter := DisTransmitterPdu.new()
	transmitter.radio_reference_id = entity.get_effective_entity_id()
	transmitter.radio_number = 1
	transmitter.radio_type = PackedInt32Array(RADIO_TYPE)
	transmitter.transmit_state = state
	transmitter.antenna_location_ecef = origin.local_to_ecef(_call_unit.global_position)
	transmitter.frequency = RADIO_FREQUENCY
	transmitter.bandwidth = 25000.0
	transmitter.power = 40.0
	transmitter.modulation_type = PackedInt32Array([0, 2, 2, 1])  # amplitude modulation, AM, generic radio
	network.send_pdu(transmitter)
	_count_sent(transmitter)


func _count_sent_type(type: int) -> void:
	_sent_types[type] = _sent_types.get(type, 0) + 1


func _remove_under_cursor() -> void:
	var mouse := _map.to_map(_map.get_local_mouse_position())
	var nearest: DisRemoteEntity = null
	var best := 3000.0
	for remote: DisRemoteEntity in manager.get_entities():
		var d := Vector2(remote.global_position.x, remote.global_position.z).distance_to(mouse)
		if d < best:
			best = d
			nearest = remote
	if nearest == null:
		_event("No remote unit within 3 km of the mouse")
		return
	_request_remove(nearest.entity_id)


func _request_remove(entity_id: Vector3i) -> void:
	var name := _unit_name(entity_id)
	var id := exercise.send_remove_entity(entity_id)
	_count_sent_type(DisPdu.PDU_TYPE_REMOVE_ENTITY)
	_event("[color=#9cf]REMOVE[/color] requested for %s (%s), request #%d" % [name, _id_text(entity_id), id])


func _on_started(pdu: DisStartResumePdu) -> void:
	_effects.banner = ""
	_event("[color=#8f8]EXERCISE RUNNING[/color]%s" % ("" if pdu == null else " (Start/Resume from %s)" %
			_id_text(pdu.originating_id)))


func _on_stopped(pdu: DisStopFreezePdu) -> void:
	var by := "" if pdu == null else " by %s: %s" % [_id_text(pdu.originating_id),
			DisEnums.get_enum_name("stop_freeze_reason", pdu.reason)]
	_effects.banner = "EXERCISE STOPPED" + by
	_event("[color=#f88]EXERCISE STOPPED[/color]%s" % by)


func _on_acknowledged(request_id: int, response: int, pdu: DisAcknowledgePdu) -> void:
	_event("ACK #%d from %s: %s, %s" % [request_id, _id_text(pdu.originating_id),
			DisEnums.get_enum_name("acknowledge_flag", pdu.acknowledge_flag),
			DisEnums.get_enum_name("acknowledge_response", response)])


func _on_remove_requested(entity_id: Vector3i, pdu: DisRemoveEntityPdu) -> void:
	for mover in own_forces.get_children():
		if _own_entity(mover).get_effective_entity_id() == entity_id:
			_event("[color=#9cf]%s removed[/color] on request of %s" % [mover.name, _id_text(pdu.originating_id)])
			mover.queue_free()  # its DisEntity sends a final "deactivated" Entity State PDU


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if key == null or not key.pressed or key.echo:
		return
	match key.keycode:
		KEY_F:
			if exercise.is_running():
				_fire_once()
		KEY_X:
			_remove_under_cursor()
		KEY_S:
			_stop_exercise()
		KEY_R:
			_resume_exercise()
		KEY_T:
			if exercise.is_running():
				_start_call()


# ---------------------------------------------------------------------------------------------------------------
# Frame

func _process(delta: float) -> void:
	var units := _collect_units()
	_map.set_units(units, delta if exercise.is_running() else 0.0)
	var elapsed := Time.get_ticks_msec() / 1000.0 - _started
	if exercise.is_running():
		_process_shots()
		_process_call(delta)
		if _auto_fire > 0.0:
			_auto_fire_clock += delta
			if _auto_fire_clock >= _auto_fire:
				_auto_fire_clock = 0.0
				_fire_once()
	if _stop_at >= 0.0 and elapsed >= _stop_at:
		_stop_at = -1.0
		_stop_exercise()
	if _resume_at >= 0.0 and elapsed >= _resume_at:
		_resume_at = -1.0
		_resume_exercise()
	if _radio_at >= 0.0 and elapsed >= _radio_at:
		_radio_at = -1.0
		_start_call()
	if _remove_at >= 0.0 and elapsed >= _remove_at:
		_remove_at = -1.0
		_request_remove(_remove_id)
	if _capture_dir != "":
		_capture(elapsed)
	_panel_clock += delta
	if _panel_clock >= 0.25:
		_panel_clock = 0.0
		_update_panel(units)
	if _quit_after > 0.0 and elapsed >= _quit_after:
		_quit()


func _capture(elapsed: float) -> void:
	# One frame per 1 / CAPTURE_FPS of real time; a slow frame is repeated so the clip keeps real-time speed.
	var image: Image = null
	while _capture_frame < elapsed * CAPTURE_FPS:
		if image == null:
			image = get_viewport().get_texture().get_image()
		var path := _capture_dir.path_join("f%08d.png" % _capture_frame)
		_capture_tasks.append(WorkerThreadPool.add_task(func(): image.save_png(path)))
		_capture_frame += 1


func _type_counts(counts: Dictionary) -> String:
	var parts := PackedStringArray()
	var types := counts.keys()
	types.sort()
	for type in types:
		parts.append("%s %d" % [DisEnums.get_enum_name("pdu_type", type), counts[type]])
	return ", ".join(parts) if not parts.is_empty() else "none"


func _quit() -> void:
	for task in _capture_tasks:
		WorkerThreadPool.wait_for_task_completion(task)
	if _capture_dir != "":
		print("captured %d frames" % _capture_frame)
	if _screenshot != "":
		get_viewport().get_texture().get_image().save_png(_screenshot)
		print("screenshot saved: ", _screenshot)
	var stats := network.get_statistics()
	print("%s: sent %d, received %d, own %d, remote %d, exercise %s" % [side, stats.sent, stats.received,
			own_forces.get_child_count(), manager.get_entity_count(),
			"running" if exercise.is_running() else "stopped"])
	print("  received by type: ", _type_counts(_received_types))
	if _recorder != null:
		print("  recorded %d packets to %s" % [_recorder.get_packet_count(), _recorder.get_recording_path()])
		_recorder.stop()
	print("  sent by type (besides Entity State): ", _type_counts(_sent_types))
	for mover in own_forces.get_children():
		var entity := _own_entity(mover)
		print("  %s: %d PDUs%s" % [mover.name, entity.get_published_count(),
				", destroyed" if DisEnums.appearance_to_status(entity.appearance) == 4 else ""])
	get_tree().quit()


func _event(text: String) -> void:
	var time := Time.get_time_string_from_system()
	_events.append("[color=#789]%s[/color] %s" % [time, text])
	if _events.size() > EVENT_LINES:
		_events = _events.slice(_events.size() - EVENT_LINES)
	if _log != null:
		_log.text = "\n".join(_events)
	print_rich("[%s] %s" % [side, text])


func _update_panel(units: Array) -> void:
	var stats := network.get_statistics()
	var cursor := ""
	if _map.cursor_geodetic.size() == 3:
		cursor = "\nCursor: %.5f° N  %.5f° E" % [_map.cursor_geodetic[0], _map.cursor_geodetic[1]]
	_status.text = ("Multicast %s:%d  site %d  exercise %s\nSent %d  received %d  ignored %d  rejected %d\n"
			+ "Own %d  remote %d%s%s") % [network.multicast_group, network.port, network.site_id,
			"RUNNING" if exercise.is_running() else "STOPPED", stats.sent,
			stats.received, stats.ignored, stats.rejected, own_forces.get_child_count(), manager.get_entity_count(),
			cursor, "" if _map.has_milsymbol() else "\n(godot-milsymbol not found: simple markers)"]
	var lines := PackedStringArray()
	units.sort_custom(func(a, b): return [a.force, a.marking] < [b.force, b.marking])
	for unit in units:
		var geodetic := origin.local_to_geodetic(unit.position)
		var color := DemoTacticalMap.color_of(unit.force).to_html(false)
		lines.append("[color=#%s][b]%s[/b][/color]  %s%s\n   [color=#9aa]%s · %s · %.4f, %.4f · %d km/h%s[/color]" % [
				color, unit.marking, unit.name, "  [color=#f66]destroyed[/color]" if unit.destroyed else "",
				DisEnums.get_enum_name("force_id", unit.force), unit.source,
				geodetic[0], geodetic[1], roundi(unit.speed * 3.6),
				" · %d PDU" % unit.published if unit.published >= 0 else ""])
	_list.text = "\n".join(lines)


func _build_ui(title: String) -> void:
	var layer := CanvasLayer.new()
	add_child(layer)
	var root := HBoxContainer.new()
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	layer.add_child(root)

	_map = DemoTacticalMap.new()
	_map.origin = origin
	_map.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	root.add_child(_map)
	_effects = DemoMapEffects.new()
	_effects.map = _map
	_map.add_child(_effects)

	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(400, 0)
	root.add_child(panel)
	var box := VBoxContainer.new()
	panel.add_child(box)
	var heading := Label.new()
	heading.text = "godot-dis demo\n" + title
	heading.add_theme_font_size_override("font_size", 18)
	box.add_child(heading)
	_status = Label.new()
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_status.add_theme_font_size_override("font_size", 12)
	box.add_child(_status)
	box.add_child(HSeparator.new())
	_list = RichTextLabel.new()
	_list.bbcode_enabled = true
	_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_list.size_flags_stretch_ratio = 1.4
	_list.add_theme_font_size_override("normal_font_size", 12)
	_list.add_theme_font_size_override("bold_font_size", 13)
	box.add_child(_list)
	box.add_child(HSeparator.new())
	_log = RichTextLabel.new()
	_log.bbcode_enabled = true
	_log.scroll_following = true
	_log.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_log.add_theme_font_size_override("normal_font_size", 11)
	_log.add_theme_font_size_override("bold_font_size", 11)
	box.add_child(_log)
	var help := Label.new()
	help.text = ("F: fire at nearest enemy   X: remove unit under mouse   T: radio call\nS: stop exercise   R: resume   "
			+ "Wheel: zoom   Drag: pan\nSymbols: MIL-STD-2525D (approx.)\n"
			+ "Enumerations: SISO-REF-010, reprinted with permission from SISO Inc.")
	help.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	help.add_theme_font_size_override("font_size", 10)
	help.modulate = Color(1, 1, 1, 0.6)
	box.add_child(help)
