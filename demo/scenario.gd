class_name DemoScenario
## Own units of each demo side. Positions are scene metres around the origin (Ankara): x east, z south.
## Entity types are SISO-REF-010 values; names come from DisEnums at run time.

const SIDES := {
	"blue": {"site": 1, "force": 1, "title": "Blue (Friendly, site 1)"},
	"red": {"site": 2, "force": 2, "title": "Red (Opposing, site 2)"},
}

const TURKIYE := 218
const OTHER_COUNTRY := 0


## [{marking, entity_type, route, speed, altitude, dr_algorithm, start}] for `side`.
static func units(side: String) -> Array:
	if side == "red":
		var advance := PackedVector3Array([Vector3(22000, 0, -4000), Vector3(9000, 0, -1500),
				Vector3(6000, 0, 3500), Vector3(18000, 0, 6000)])
		return [
			_unit("R-T1", [1, 1, OTHER_COUNTRY, 1, 0, 0, 0], advance, 11.0),
			_unit("R-T2", [1, 1, OTHER_COUNTRY, 1, 0, 0, 0], advance, 11.0, 0.0, 2, 2500.0),
			_unit("R-AFV", [1, 1, OTHER_COUNTRY, 2, 0, 0, 0], advance, 11.0, 0.0, 2, 5000.0),
			_unit("R-SPA", [1, 1, OTHER_COUNTRY, 4, 0, 0, 0],
					PackedVector3Array([Vector3(26000, 0, 2000), Vector3(26500, 0, 2600)]), 1.5),
			_unit("R-AD", [1, 1, OTHER_COUNTRY, 28, 0, 0, 0],
					PackedVector3Array([Vector3(24000, 0, -9000), Vector3(24000, 0, -9001)]), 0.0),
			_unit("R-FTR", [1, 2, OTHER_COUNTRY, 1, 0, 0, 0],
					DemoUnitMover.circle(Vector2(25000, -2000), 12000, 180), 230.0, 7000.0, 4),
		]
	var road := PackedVector3Array([Vector3(-18000, 0, 6000), Vector3(-8000, 0, 2500), Vector3(-2000, 0, -2500),
			Vector3(-6000, 0, -9000), Vector3(-16000, 0, -6000)])
	return [
		_unit("T-1", [1, 1, TURKIYE, 1, 2, 0, 0], road, 12.0),
		_unit("T-2", [1, 1, TURKIYE, 1, 2, 0, 0], road, 12.0, 0.0, 2, 2500.0),
		_unit("ACV-1", [1, 1, TURKIYE, 2, 1, 0, 0], road, 12.0, 0.0, 2, 5000.0),
		_unit("HEL-1", [1, 2, TURKIYE, 20, 0, 0, 0], DemoUnitMover.circle(Vector2(-4000, -1000), 5000, 120), 60.0, 400.0, 4),
		_unit("F4-1", [1, 2, TURKIYE, 1, 1, 0, 0], DemoUnitMover.circle(Vector2(-10000, 0), 18000, 180), 220.0, 6000.0, 4),
	]


static func _unit(marking: String, entity_type: Array, route: PackedVector3Array, speed: float,
		altitude := 0.0, dr_algorithm := 2, start := 0.0) -> Dictionary:
	return {"marking": marking, "entity_type": PackedInt32Array(entity_type), "route": route, "speed": speed,
			"altitude": altitude, "dr_algorithm": dr_algorithm, "start": start}
