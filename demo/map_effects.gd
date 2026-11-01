class_name DemoMapEffects
extends Control
## Overlay for DemoTacticalMap: weapon fire lines, detonation bursts, destroyed units, radio transmissions and the
## exercise state banner.
## Add it as a child of the map (it uses the map's to_screen()); it draws nothing else, so the map works without it.

const FIRE_LIFE := 2.5  # seconds a fire line stays after the round arrives
const BURST_LIFE := 2.0

var map: DemoTacticalMap
## Shown across the top while not empty (e.g. "EXERCISE STOPPED").
var banner := ""

var _effects: Array = []  # {kind, from, to, color, born, flight}


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)


static func _now() -> float:
	return Time.get_ticks_msec() / 1000.0


## A shot from `from` towards `to` (scene metres); the round takes `flight` seconds.
func add_fire(from: Vector3, to: Vector3, color: Color, flight: float) -> void:
	_effects.append({"kind": "fire", "from": from, "to": to, "color": color, "born": _now(), "flight": flight})


## A detonation at `at`. `hit` draws a bigger, brighter burst.
func add_burst(at: Vector3, color: Color, hit: bool) -> void:
	_effects.append({"kind": "burst", "from": at, "to": at, "color": color, "born": _now(), "flight": 0.0,
			"hit": hit})


func _process(_delta: float) -> void:
	var now := _now()
	_effects = _effects.filter(func(e): return now - e.born < e.flight + (FIRE_LIFE if e.kind == "fire" else BURST_LIFE))
	queue_redraw()


func _point(p: Vector3) -> Vector2:
	return map.to_screen(Vector2(p.x, p.z))


func _draw() -> void:
	if map == null:
		return
	var now := _now()
	for e in _effects:
		var age: float = now - e.born
		if e.kind == "fire":
			var a := _point(e.from)
			var b := _point(e.to)
			var t := clampf(age / maxf(e.flight, 0.001), 0.0, 1.0)
			var fade := 1.0 - clampf((age - e.flight) / FIRE_LIFE, 0.0, 1.0)
			draw_dashed_line(a, b, Color(e.color, 0.55 * fade), 1.5, 6.0)
			if t < 1.0:
				draw_circle(a.lerp(b, t), 3.0, Color(1, 0.95, 0.6))  # the round in flight
			draw_circle(a, 5.0 * (1.0 - t) + 1.0, Color(1, 0.8, 0.3, 0.8 * fade))  # muzzle flash
		else:
			var p := _point(e.from)
			var k := clampf(age / BURST_LIFE, 0.0, 1.0)
			var radius := (22.0 if e.hit else 14.0) * (0.3 + k)
			draw_circle(p, radius * 0.6, Color(1, 0.7, 0.2, 0.5 * (1.0 - k)))
			draw_arc(p, radius, 0, TAU, 32, Color(e.color, 1.0 - k), 2.0)
	for unit in map.units:
		if unit.get("transmitting", false):
			_draw_radio_waves(_point(unit.position), now)
		if unit.get("destroyed", false) and not map.has_milsymbol():
			var p := _point(unit.position)
			draw_line(p + Vector2(-10, -10), p + Vector2(10, 10), Color(1, 1, 1, 0.9), 2.5)
			draw_line(p + Vector2(-10, 10), p + Vector2(10, -10), Color(1, 1, 1, 0.9), 2.5)
	if banner != "":
		var font := ThemeDB.fallback_font
		var width := font.get_string_size(banner, HORIZONTAL_ALIGNMENT_LEFT, -1, 18).x + 32
		var rect := Rect2(Vector2((size.x - width) / 2, 36), Vector2(width, 32))
		draw_rect(rect, Color(0.6, 0.1, 0.1, 0.85))
		draw_string(font, rect.position + Vector2(16, 23), banner, HORIZONTAL_ALIGNMENT_LEFT, -1, 18, Color.WHITE)


## Three arcs to the upper right of a unit that is on the air, pulsing outwards.
func _draw_radio_waves(p: Vector2, now: float) -> void:
	var center := p + Vector2(14, -14)
	for i in 3:
		var phase := fmod(now * 1.5 + i / 3.0, 1.0)
		var radius := 4.0 + 12.0 * phase
		draw_arc(center, radius, -PI * 0.75, -PI * 0.25, 10, Color(0.75, 0.9, 1.0, 1.0 - phase), 2.0)
