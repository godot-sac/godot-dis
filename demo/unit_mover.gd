class_name DemoUnitMover
extends Node3D
## Moves a demo unit around a closed route at constant speed and faces it along the route. A DisEntity child
## publishes the result, so the movement is all other simulations ever see of it.

## Route in scene metres (x east, z south; the y of the points is ignored, `altitude` is used).
var route: PackedVector3Array = []
var speed := 10.0  # m/s
var altitude := 0.0  # m above the origin's tangent plane
var distance := 0.0  # along the route

var _lengths: PackedFloat64Array = []
var _total := 0.0


func _ready() -> void:
	_lengths.clear()
	_total = 0.0
	for i in route.size():
		var segment := route[i].distance_to(route[(i + 1) % route.size()])
		_lengths.append(segment)
		_total += segment
	_place()


func _physics_process(delta: float) -> void:
	distance = fmod(distance + speed * delta, maxf(_total, 0.001))
	_place()


func _place() -> void:
	if route.size() < 2:
		return
	var remaining := distance
	for i in route.size():
		if remaining <= _lengths[i] or i == route.size() - 1:
			var from := route[i]
			var to := route[(i + 1) % route.size()]
			var t := clampf(remaining / maxf(_lengths[i], 0.001), 0.0, 1.0)
			var point := from.lerp(to, t)
			position = Vector3(point.x, altitude, point.z)
			var direction := Vector3(to.x - from.x, 0, to.z - from.z)
			if direction.length() > 0.001:
				basis = Basis.looking_at(direction.normalized(), Vector3.UP)
			return
		remaining -= _lengths[i]


## A closed polygon approximating a circle (for aircraft orbits).
static func circle(center: Vector2, radius: float, points := 36) -> PackedVector3Array:
	var result := PackedVector3Array()
	for i in points:
		var angle := TAU * i / points
		result.append(Vector3(center.x + radius * cos(angle), 0, center.y + radius * sin(angle)))
	return result
