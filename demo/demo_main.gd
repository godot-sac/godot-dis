extends Node
## godot-dis demo: one simulation of a two-sided exercise around Ankara.
##
## Run two copies (and optionally tools/demo_traffic.py) on one computer or on a LAN:
##   <godot> --path . -- --side blue
##   <godot> --path . -- --side red
## Each copy publishes its own units with DisEntity and shows every unit on the network (its own and the others')
## on a tactical map. Everyone uses multicast 239.1.2.3:3000, so copies on one computer share the port.
## See docs/demo.md.

@onready var network: DisNetwork = $DisNetwork
@onready var origin: DisGeoOrigin = $DisGeoOrigin
@onready var manager: DisEntityManager = $DisEntityManager
@onready var own_forces: Node3D = $OwnForces

var side := "blue"
var _map: DemoTacticalMap
var _status: Label
var _list: RichTextLabel
var _type_cache := {}  # "k:d:c:..." + force -> {name, sidc}
var _panel_clock := 0.0
# Optional: --screenshot <file.png> saves the window after --seconds; --seconds alone quits after that time.
var _screenshot := ""
var _quit_after := 0.0
var _started := 0.0


func _ready() -> void:
	side = _argument("--side", "blue")
	if not DemoScenario.SIDES.has(side):
		push_warning("unknown side '%s', using blue" % side)
		side = "blue"
	var info: Dictionary = DemoScenario.SIDES[side]
	network.site_id = info.site
	var port := int(_argument("--port", str(network.port)))
	if port != network.port:  # --port: a separate exercise, e.g. for tests next to a running demo
		network.stop()
		network.port = port
		network.start()
	get_window().title = "godot-dis demo — %s" % info.title
	_build_ui(info.title)
	_spawn_units(info)
	_screenshot = _argument("--screenshot", "")
	_quit_after = float(_argument("--seconds", "0"))
	_started = Time.get_ticks_msec() / 1000.0


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


func _type_info(entity_type: PackedInt32Array, force: int) -> Dictionary:
	var key := "%s/%d" % [entity_type, force]
	if not _type_cache.has(key):
		_type_cache[key] = {"name": DisEnums.get_entity_type_name(entity_type),
				"sidc": DisEnums.entity_type_to_sidc(entity_type, force)}
	return _type_cache[key]


static func _heading(basis: Basis) -> float:
	var forward := -basis.z
	return atan2(forward.x, -forward.z)  # clockwise from north (-Z)


func _collect_units() -> Array:
	var result := []
	for mover: DemoUnitMover in own_forces.get_children():
		var entity: DisEntity = mover.get_child(0)
		var info := _type_info(entity.entity_type, entity.force_id)
		result.append({"key": "own/" + entity.marking, "marking": entity.marking, "name": info.name,
				"sidc": info.sidc, "force": entity.force_id, "position": mover.global_position,
				"heading": _heading(mover.global_basis), "speed": mover.speed,
				"source": "own %d:%d:%d" % [entity.get_effective_entity_id().x, entity.get_effective_entity_id().y,
						entity.get_effective_entity_id().z],
				"published": entity.get_published_count()})
	for remote: DisRemoteEntity in manager.get_entities():
		var pdu := remote.get_last_pdu()
		var info := _type_info(remote.entity_type, remote.force_id)
		var velocity := origin.ecef_vector_to_local(pdu.linear_velocity)
		result.append({"key": "remote/%s" % remote.entity_id, "marking": remote.marking, "name": info.name,
				"sidc": info.sidc, "force": remote.force_id, "position": remote.global_position,
				"heading": _heading(remote.global_basis), "speed": Vector2(velocity.x, velocity.z).length(),
				"source": "%d:%d:%d" % [remote.entity_id.x, remote.entity_id.y, remote.entity_id.z],
				"published": -1})
	return result


func _process(delta: float) -> void:
	var units := _collect_units()
	_map.set_units(units, delta)
	_panel_clock += delta
	if _panel_clock >= 0.25:
		_panel_clock = 0.0
		_update_panel(units)
	if _quit_after > 0.0 and Time.get_ticks_msec() / 1000.0 - _started >= _quit_after:
		if _screenshot != "":
			get_viewport().get_texture().get_image().save_png(_screenshot)
			print("screenshot saved: ", _screenshot)
		var stats := network.get_statistics()
		print("%s: sent %d, received %d, own %d, remote %d" % [side, stats.sent, stats.received,
				own_forces.get_child_count(), manager.get_entity_count()])
		for mover in own_forces.get_children():
			print("  %s: %d PDUs" % [mover.name, (mover.get_child(0) as DisEntity).get_published_count()])
		get_tree().quit()


func _update_panel(units: Array) -> void:
	var stats := network.get_statistics()
	var cursor := ""
	if _map.cursor_geodetic.size() == 3:
		cursor = "\nCursor: %.5f° N  %.5f° E" % [_map.cursor_geodetic[0], _map.cursor_geodetic[1]]
	_status.text = ("Multicast %s:%d  site %d\nSent %d  received %d  ignored %d  rejected %d\n"
			+ "Own %d  remote %d%s%s") % [network.multicast_group, network.port, network.site_id, stats.sent,
			stats.received, stats.ignored, stats.rejected, own_forces.get_child_count(), manager.get_entity_count(),
			cursor, "" if _map.has_milsymbol() else "\n(godot-milsymbol not found: simple markers)"]
	var lines := PackedStringArray()
	units.sort_custom(func(a, b): return [a.force, a.marking] < [b.force, b.marking])
	for unit in units:
		var geodetic := origin.local_to_geodetic(unit.position)
		var color := DemoTacticalMap.color_of(unit.force).to_html(false)
		lines.append("[color=#%s][b]%s[/b][/color]  %s\n   [color=#9aa]%s · %s · %.4f, %.4f · %d km/h%s[/color]" % [
				color, unit.marking, unit.name, DisEnums.get_enum_name("force_id", unit.force), unit.source,
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

	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(380, 0)
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
	_list.add_theme_font_size_override("normal_font_size", 12)
	_list.add_theme_font_size_override("bold_font_size", 13)
	box.add_child(_list)
	var help := Label.new()
	help.text = "Wheel: zoom   Drag: pan\nSymbols: MIL-STD-2525D (approx.)\nEnumerations: SISO-REF-010, reprinted with permission from SISO Inc."
	help.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	help.add_theme_font_size_override("font_size", 10)
	help.modulate = Color(1, 1, 1, 0.6)
	box.add_child(help)
