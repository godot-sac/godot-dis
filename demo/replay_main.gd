extends Node
## godot-dis replay demo: plays a DIS recording (pcapng / pcap / .pdulog) back on the tactical map, for after
## action review. Record one with the exercise demo (`--record user://exercise.pcapng`) or the command line tool,
## or open any DIS capture made with Wireshark.
##   <godot> --path . res://demo/replay.tscn -- --file user://exercise.pcapng
## Keys: Space play / pause, Left / Right 10 s back / forward, Up / Down speed x2 / /2, Home to the start.
## The timeline slider seeks. Options: --speed X, --seek S, --loop, --rebroadcast (also send onto the network),
## --seconds N and --screenshot file.png for unattended runs. See docs/recording.md.

const SPEEDS := [0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0]
const EVENT_LINES := 14

@onready var network: DisNetwork = $DisNetwork
@onready var origin: DisGeoOrigin = $DisGeoOrigin
@onready var manager: DisEntityManager = $DisEntityManager
@onready var player: DisPlayer = $DisPlayer

var _map: DemoTacticalMap
var _effects: DemoMapEffects
var _status: Label
var _timeline: HSlider
var _play_button: Button
var _log: RichTextLabel
var _events: PackedStringArray = []
var _type_cache := {}
var _updating_slider := false
var _screenshot := ""
var _quit_after := 0.0
var _started := 0.0


func _ready() -> void:
	_build_ui()
	var file := _argument("--file", "user://exercise.pcapng")
	_screenshot = _argument("--screenshot", "")
	_quit_after = float(_argument("--seconds", "0"))
	_started = Time.get_ticks_msec() / 1000.0
	if OS.get_cmdline_user_args().has("--rebroadcast"):
		player.output = DisPlayer.OUTPUT_BOTH
		network.start()
	player.loop = OS.get_cmdline_user_args().has("--loop")
	network.fire_received.connect(_on_fire)
	network.detonation_received.connect(_on_detonation)
	network.stop_freeze_received.connect(func(pdu): _event("[color=#f88]STOP/FREEZE[/color] from %s: %s" % [
			_id(pdu.originating_id), DisEnums.get_enum_name("stop_freeze_reason", pdu.reason)]))
	network.start_resume_received.connect(func(pdu): _event("[color=#8f8]START/RESUME[/color] from %s" %
			_id(pdu.originating_id)))
	network.remove_entity_received.connect(func(pdu): _event("[color=#9cf]REMOVE[/color] %s requested by %s" % [
			_id(pdu.receiving_id), _id(pdu.originating_id)]))
	player.seeked.connect(func(position): _event("Jumped to %s" % _clock(position)))
	player.finished.connect(func(): _event("End of recording"))
	if player.load(file) != OK:
		_event("[color=#f66]Cannot open %s[/color]: %s. Record one first: res://demo/main.tscn -- --record %s" % [
				file, player.recording.get_last_error() if player.recording else "file not found", file])
		return
	var summary := player.recording.get_summary()
	_event("Opened %s: %d packets, %s, %d entities" % [file.get_file(), summary.packets, _clock(summary.duration),
			summary.entities])
	_timeline.max_value = player.get_duration()
	player.speed = float(_argument("--speed", "1"))
	player.seek(float(_argument("--seek", "0")))
	player.play()


func _argument(name: String, default: String) -> String:
	var args := OS.get_cmdline_user_args()
	var i := args.find(name)
	return args[i + 1] if i >= 0 and i + 1 < args.size() else default


static func _id(v: Vector3i) -> String:
	return "%d:%d:%d" % [v.x, v.y, v.z]


static func _clock(seconds: float) -> String:
	return "%d:%04.1f" % [int(seconds) / 60, fmod(seconds, 60.0)]


func _name_of(id: Vector3i) -> String:
	var remote := manager.get_entity(id) as DisRemoteEntity
	return remote.marking if remote != null else _id(id)


func _on_fire(pdu: DisFirePdu) -> void:
	var from := origin.ecef_to_local(pdu.location_ecef)
	var target := manager.get_entity(pdu.target_entity_id) as DisRemoteEntity
	var shooter := manager.get_entity(pdu.firing_entity_id) as DisRemoteEntity
	var to := target.global_position if target != null else from + Vector3(0, 0, -500)
	var color := DemoTacticalMap.color_of(shooter.force_id) if shooter != null else DemoTacticalMap.UNKNOWN_COLOR
	_effects.add_fire(from, to, color, clampf(1.0 / maxf(player.speed, 0.1), 0.2, 3.0))
	_event("[color=#fd8]FIRE[/color] %s → %s" % [_name_of(pdu.firing_entity_id), _name_of(pdu.target_entity_id)])


func _on_detonation(pdu: DisDetonationPdu) -> void:
	var hit := pdu.detonation_result == DisDetonationPdu.RESULT_ENTITY_IMPACT
	_effects.add_burst(origin.ecef_to_local(pdu.location_ecef), Color(1, 0.6, 0.3) if hit else Color(0.8, 0.8, 0.6), hit)
	_event("[color=#f96]DETONATION[/color] %s%s" % [DisEnums.get_enum_name("detonation_result", pdu.detonation_result),
			" on " + _name_of(pdu.target_entity_id) if hit else ""])


func _event(text: String) -> void:
	_events.append("[color=#789]%s[/color] %s" % [_clock(player.get_position()), text])
	if _events.size() > EVENT_LINES:
		_events = _events.slice(_events.size() - EVENT_LINES)
	_log.text = "\n".join(_events)
	print_rich("[replay] %s" % text)


func _units() -> Array:
	var result := []
	for remote: DisRemoteEntity in manager.get_entities():
		var status := DisEnums.appearance_to_status(remote.get_appearance())
		var key := "%s/%d/%d" % [remote.entity_type, remote.force_id, status]
		if not _type_cache.has(key):
			_type_cache[key] = {"name": DisEnums.get_entity_type_name(remote.entity_type),
					"sidc": DisEnums.entity_type_to_sidc(remote.entity_type, remote.force_id, status)}
		var forward := -remote.global_basis.z
		var velocity := origin.ecef_vector_to_local(remote.get_last_pdu().linear_velocity)
		result.append({"key": str(remote.entity_id), "marking": remote.marking, "name": _type_cache[key].name,
				"sidc": _type_cache[key].sidc, "force": remote.force_id, "position": remote.global_position,
				"heading": atan2(forward.x, -forward.z), "speed": Vector2(velocity.x, velocity.z).length(),
				"destroyed": status == 4})
	return result


func _process(delta: float) -> void:
	_map.set_units(_units(), delta * (player.speed if player.is_playing() else 0.0))
	_updating_slider = true
	_timeline.value = player.get_position()
	_updating_slider = false
	_play_button.text = "Pause" if player.is_playing() else "Play"
	_status.text = "%s / %s   speed %sx   %s\nEntities %d   played %d packets" % [_clock(player.get_position()),
			_clock(player.get_duration()), player.speed, "playing" if player.is_playing() else "paused",
			manager.get_entity_count(), network.get_statistics().injected]
	if _quit_after > 0.0 and Time.get_ticks_msec() / 1000.0 - _started >= _quit_after:
		if _screenshot != "":
			get_viewport().get_texture().get_image().save_png(_screenshot)
			print("screenshot saved: ", _screenshot)
		print("replay: position %.1f / %.1f s, %d entities, %d packets played" % [player.get_position(),
				player.get_duration(), manager.get_entity_count(), network.get_statistics().injected])
		get_tree().quit()


func _change_speed(direction: int) -> void:
	var i := SPEEDS.find(player.speed)
	i = clampi((i if i >= 0 else SPEEDS.find(1.0)) + direction, 0, SPEEDS.size() - 1)
	player.speed = SPEEDS[i]


func _toggle() -> void:
	if player.is_playing():
		player.pause()
	else:
		player.play()


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if key == null or not key.pressed:
		return
	match key.keycode:
		KEY_SPACE:
			_toggle()
		KEY_LEFT:
			player.seek(player.get_position() - 10.0)
		KEY_RIGHT:
			player.seek(player.get_position() + 10.0)
		KEY_UP:
			_change_speed(1)
		KEY_DOWN:
			_change_speed(-1)
		KEY_HOME:
			player.seek(0.0)


func _build_ui() -> void:
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
	heading.text = "godot-dis replay"
	heading.add_theme_font_size_override("font_size", 18)
	box.add_child(heading)
	_status = Label.new()
	_status.add_theme_font_size_override("font_size", 12)
	box.add_child(_status)
	_timeline = HSlider.new()
	_timeline.step = 0.1
	_timeline.value_changed.connect(func(value):
		if not _updating_slider:
			player.seek(value))
	box.add_child(_timeline)
	var buttons := HBoxContainer.new()
	box.add_child(buttons)
	for spec in [["|<", func(): player.seek(0.0)], ["-10 s", func(): player.seek(player.get_position() - 10.0)],
			["Play", _toggle], ["+10 s", func(): player.seek(player.get_position() + 10.0)],
			["Slower", func(): _change_speed(-1)], ["Faster", func(): _change_speed(1)]]:
		var button := Button.new()
		button.text = spec[0]
		button.focus_mode = Control.FOCUS_NONE
		button.pressed.connect(spec[1])
		buttons.add_child(button)
		if spec[0] == "Play":
			_play_button = button
	box.add_child(HSeparator.new())
	_log = RichTextLabel.new()
	_log.bbcode_enabled = true
	_log.scroll_following = true
	_log.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_log.add_theme_font_size_override("normal_font_size", 11)
	box.add_child(_log)
	var help := Label.new()
	help.text = ("Space: play / pause   Left / Right: 10 s   Up / Down: speed   Home: start\n"
			+ "Wheel: zoom   Drag: pan\nEnumerations: SISO-REF-010, reprinted with permission from SISO Inc.")
	help.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	help.add_theme_font_size_override("font_size", 10)
	help.modulate = Color(1, 1, 1, 0.6)
	box.add_child(help)
