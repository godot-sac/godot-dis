class_name DemoTacticalMap
extends Control
## Top-down tactical map of the demo: km grid around the origin, a symbol, label and trail per unit.
## Units come from demo_main.gd as dictionaries (see DemoMain._collect_units). Mouse wheel zooms, dragging pans.
## Symbols are drawn with godot-milsymbol if it is in the project (addons/milsymbol), otherwise as simple shapes.

# By MIL-STD-2525D standard identity (DisEnums.force_to_standard_identity): friend, hostile, neutral, unknown.
const IDENTITY_COLORS := {"3": Color(0.5, 0.85, 1.0), "6": Color(1.0, 0.5, 0.5), "4": Color(0.65, 1.0, 0.65)}
const UNKNOWN_COLOR := Color(1.0, 1.0, 0.5)
const SYMBOL_OPTIONS := {"size": 28}
const TRAIL_INTERVAL := 1.0  # seconds between trail points
const TRAIL_POINTS := 90

var origin: DisGeoOrigin
var units: Array = []
var meters_per_pixel := 20.0
var center := Vector2(1500, 0)  # map metres (x east, z south) shown at the middle of the control
var cursor_geodetic := PackedFloat64Array()

var _milsymbol: Script
var _trails := {}  # unit key -> PackedVector2Array (map metres)
var _trail_clock := 0.0
var _dragging := false


func _ready() -> void:
	clip_contents = true
	mouse_filter = Control.MOUSE_FILTER_STOP
	for info in ProjectSettings.get_global_class_list():
		if info["class"] == "MilSymbol":
			_milsymbol = load(info["path"])


func has_milsymbol() -> bool:
	return _milsymbol != null


func set_units(new_units: Array, delta: float) -> void:
	units = new_units
	_trail_clock += delta
	if _trail_clock >= TRAIL_INTERVAL:
		_trail_clock = 0.0
		var alive := {}
		for unit in units:
			alive[unit.key] = true
			var trail: PackedVector2Array = _trails.get(unit.key, PackedVector2Array())
			trail.append(Vector2(unit.position.x, unit.position.z))
			if trail.size() > TRAIL_POINTS:
				trail = trail.slice(trail.size() - TRAIL_POINTS)
			_trails[unit.key] = trail
		for key in _trails.keys():
			if not alive.has(key):
				_trails.erase(key)
	queue_redraw()


func to_screen(map_point: Vector2) -> Vector2:
	return size / 2 + (map_point - center) / meters_per_pixel


func to_map(screen_point: Vector2) -> Vector2:
	return center + (screen_point - size / 2) * meters_per_pixel


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		var button := event as InputEventMouseButton
		if button.button_index in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN] and button.pressed:
			var before := to_map(button.position)
			meters_per_pixel *= 0.85 if button.button_index == MOUSE_BUTTON_WHEEL_UP else 1.0 / 0.85
			meters_per_pixel = clampf(meters_per_pixel, 2.0, 500.0)
			center += before - to_map(button.position)  # keep the point under the mouse in place
			queue_redraw()
		elif button.button_index == MOUSE_BUTTON_LEFT:
			_dragging = button.pressed
	elif event is InputEventMouseMotion:
		var motion := event as InputEventMouseMotion
		if _dragging:
			center -= motion.relative * meters_per_pixel
			queue_redraw()
		if origin != null:
			var p := to_map(motion.position)
			cursor_geodetic = origin.local_to_geodetic(Vector3(p.x, 0, p.y))


func _draw() -> void:
	var font := ThemeDB.fallback_font
	draw_rect(Rect2(Vector2.ZERO, size), Color(0.07, 0.09, 0.11))
	_draw_grid(font)
	_draw_origin(font)
	for unit in units:
		_draw_trail(unit)
	for unit in units:
		_draw_unit(unit, font)
	_draw_scale_bar(font)


func _grid_step() -> float:
	for step in [500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0, 50000.0]:
		if step / meters_per_pixel >= 90.0:
			return step
	return 100000.0


func _draw_grid(font: Font) -> void:
	var step := _grid_step()
	var top_left := to_map(Vector2.ZERO)
	var bottom_right := to_map(size)
	var line := Color(1, 1, 1, 0.07)
	var label := Color(1, 1, 1, 0.35)
	var x := floorf(top_left.x / step) * step
	while x <= bottom_right.x:
		var sx := to_screen(Vector2(x, 0)).x
		draw_line(Vector2(sx, 0), Vector2(sx, size.y), line if x != 0 else Color(1, 1, 1, 0.18))
		draw_string(font, Vector2(sx + 3, 14), "%+d km" % roundi(x / 1000.0), HORIZONTAL_ALIGNMENT_LEFT, -1, 11, label)
		x += step
	var z := floorf(top_left.y / step) * step
	while z <= bottom_right.y:
		var sy := to_screen(Vector2(0, z)).y
		draw_line(Vector2(0, sy), Vector2(size.x, sy), line if z != 0 else Color(1, 1, 1, 0.18))
		# Map z grows to the south; label north positive like a map.
		draw_string(font, Vector2(4, sy - 3), "%+d km" % roundi(-z / 1000.0), HORIZONTAL_ALIGNMENT_LEFT, -1, 11, label)
		z += step


func _draw_origin(font: Font) -> void:
	var p := to_screen(Vector2.ZERO)
	draw_circle(p, 3, Color(1, 1, 1, 0.5))
	var text := "Üsküdar"
	if origin != null:
		text += "  %.4f° N  %.4f° E" % [origin.latitude, origin.longitude]
	draw_string(font, p + Vector2(6, -6), text, HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color(1, 1, 1, 0.5))
	# North arrow
	var n := Vector2(size.x - 30, 40)
	draw_colored_polygon(PackedVector2Array([n + Vector2(0, -16), n + Vector2(7, 6), n + Vector2(-7, 6)]),
			Color(1, 1, 1, 0.6))
	draw_string(font, n + Vector2(-4, 22), "N", HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color(1, 1, 1, 0.6))


func _draw_scale_bar(font: Font) -> void:
	var step := _grid_step()
	var length := step / meters_per_pixel
	var start := Vector2(20, size.y - 24)
	draw_line(start, start + Vector2(length, 0), Color(1, 1, 1, 0.7), 2)
	draw_line(start, start + Vector2(0, -6), Color(1, 1, 1, 0.7), 2)
	draw_line(start + Vector2(length, 0), start + Vector2(length, -6), Color(1, 1, 1, 0.7), 2)
	draw_string(font, start + Vector2(0, 16), "%s km" % str(step / 1000.0).trim_suffix(".0"), HORIZONTAL_ALIGNMENT_LEFT, -1, 11,
			Color(1, 1, 1, 0.7))


static func color_of(force_id: int) -> Color:
	return IDENTITY_COLORS.get(DisEnums.force_to_standard_identity(force_id), UNKNOWN_COLOR)


func _color(unit: Dictionary) -> Color:
	return color_of(unit.force)


func _draw_trail(unit: Dictionary) -> void:
	var trail: PackedVector2Array = _trails.get(unit.key, PackedVector2Array())
	if trail.size() < 2:
		return
	var color := _color(unit)
	for i in range(1, trail.size()):
		color.a = 0.5 * i / trail.size()
		draw_line(to_screen(trail[i - 1]), to_screen(trail[i]), color, 1.5)


func _draw_unit(unit: Dictionary, font: Font) -> void:
	var p := to_screen(Vector2(unit.position.x, unit.position.z))
	if not Rect2(Vector2(-60, -60), size + Vector2(120, 120)).has_point(p):
		return
	var color := _color(unit)
	var half_height := 14.0
	var texture: Texture2D = null
	if _milsymbol != null:
		texture = _milsymbol.create_texture(unit.sidc, SYMBOL_OPTIONS)
	if texture != null:
		var anchor: Vector2 = _milsymbol.render_cached(unit.sidc, SYMBOL_OPTIONS).anchor
		draw_texture(texture, p - anchor)
		half_height = texture.get_height() - anchor.y
	else:
		draw_circle(p, 8, color)
	# Heading tick for moving units.
	if unit.speed > 0.5:
		var heading_end := p + Vector2(sin(unit.heading), -cos(unit.heading)) * 26
		draw_line(p, heading_end, Color(color, 0.8), 1.5)
	var y := p.y + half_height + 12
	var title: String = unit.marking
	if unit.position.y > 50:
		title += "  %d m" % roundi(unit.position.y)
	draw_string(font, Vector2(p.x - 80, y), title, HORIZONTAL_ALIGNMENT_CENTER, 160, 12, color)
	draw_string(font, Vector2(p.x - 100, y + 13), unit.name, HORIZONTAL_ALIGNMENT_CENTER, 200, 10, Color(color, 0.7))
