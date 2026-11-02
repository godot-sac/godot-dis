class_name DemoScenario
## Own units of each demo side. Positions are scene metres around the origin (Üsküdar, Istanbul: 41.0200 N,
## 29.0500 E, ellipsoid height 0): x east, z south. The units and their positions are fictitious.
## Land routes follow main roads of the Asian side (© OpenStreetMap contributors, ODbL; via the godot-geo terrain
## package): blue between Altunizade, Çamlıca and Acıbadem, red east of the O-2 (TEM) motorway. Entity types are
## SISO-REF-010 values; names come from DisEnums at run time.

const SIDES := {
	"blue": {"site": 1, "force": 1, "title": "Blue (Friendly, site 1)"},
	"red": {"site": 2, "force": 2, "title": "Red (Opposing, site 2)"},
}

const TURKIYE := 218
const OTHER_COUNTRY := 0
## Height of the Bosphorus surface above the origin's tangent plane, m: EGM2008 geoid height at the origin
## (+37.3 m, the origin is on the ellipsoid) less ~1 m of Earth curvature at 3-5 km.
const SEA_SURFACE := 36.3

# Closed routes as x, z pairs (scene metres).
const BLUE_ROAD := [-600, -370, -330, -280, 140, -400, 600, -450, 850, -270, 960, -220, 1460, -220, 1870, -300,
		2000, -280, 2040, -170, 2030, -70, 2110, -10, 2160, 460, 2130, 520, 2100, 930, 2050, 1160, 1890, 1660,
		1870, 1630, 1480, 1600, 1050, 1470, 990, 1260, 970, 1050, 890, 880, 180, 180, 0, -30, -20, -140, 20, -300,
		100, -340, -480, -240, -400, -280]
const RED_ADVANCE := [6610, -90, 6520, -140, 6240, -90, 5890, -80, 5950, -640, 5920, -820, 5860, -920, 5630, -1140,
		5480, -1180, 5340, -1160, 5040, -1030, 4740, -950, 4600, -940, 4230, -570, 4190, -560, 5020, -450, 5400,
		-290, 5290, 140, 4780, 820, 4620, 1110, 4590, 1240, 4690, 1510, 4670, 1550, 4780, 1500, 4840, 1370, 5030,
		1260, 5280, 1250, 5490, 1180, 5670, 1150, 5790, 1080, 5950, 910, 5880, -120, 6060, -90, 6370, -120, 6490,
		-160, 6600, -270, 6560, -180]


## [{marking, entity_type, route, speed, altitude, dr_algorithm, start, surface}] for `side`.
static func units(side: String) -> Array:
	if side == "red":
		var advance := path(RED_ADVANCE)
		return [
			_unit("R-T1", [1, 1, OTHER_COUNTRY, 1, 0, 0, 0], advance, 11.0),
			_unit("R-T2", [1, 1, OTHER_COUNTRY, 1, 0, 0, 0], advance, 11.0, 0.0, 2, 600.0),
			_unit("R-AFV", [1, 1, OTHER_COUNTRY, 2, 0, 0, 0], advance, 11.0, 0.0, 2, 1200.0),
			_unit("R-SPA", [1, 1, OTHER_COUNTRY, 4, 0, 0, 0],
					PackedVector3Array([Vector3(8000, 0, 1500), Vector3(8300, 0, 1900)]), 1.5),
			_unit("R-AD", [1, 1, OTHER_COUNTRY, 28, 0, 0, 0],
					PackedVector3Array([Vector3(5200, 0, -1600), Vector3(5200, 0, -1601)]), 0.0),
			_unit("R-FTR", [1, 2, OTHER_COUNTRY, 1, 0, 0, 0],
					DemoUnitMover.circle(Vector2(7000, -1000), 7000, 120), 230.0, 7000.0, 4),
		]
	var road := path(BLUE_ROAD)
	var patrol := _unit("P-1", [1, 3, TURKIYE, 7, 4, 0, 0],  # light / patrol craft in the Bosphorus
			DemoUnitMover.racetrack(Vector2(-4700, 0), Vector2(-2600, -2000), 300), 9.0, SEA_SURFACE)
	patrol.surface = true
	return [
		_unit("T-1", [1, 1, TURKIYE, 1, 2, 0, 0], road, 12.0),
		_unit("T-2", [1, 1, TURKIYE, 1, 2, 0, 0], road, 12.0, 0.0, 2, 500.0),
		_unit("ACV-1", [1, 1, TURKIYE, 2, 1, 0, 0], road, 12.0, 0.0, 2, 1000.0),
		_unit("HEL-1", [1, 2, TURKIYE, 20, 0, 0, 0], DemoUnitMover.circle(Vector2(800, 200), 2500, 72), 60.0, 500.0, 4),
		_unit("F4-1", [1, 2, TURKIYE, 1, 1, 0, 0], DemoUnitMover.circle(Vector2(-2000, 0), 9000, 120), 220.0, 6000.0, 4),
		patrol,
	]


## PackedVector3Array from flat x, z pairs.
static func path(xz: Array) -> PackedVector3Array:
	var result := PackedVector3Array()
	for i in range(0, xz.size() - 1, 2):
		result.append(Vector3(xz[i], 0, xz[i + 1]))
	return result


static func _unit(marking: String, entity_type: Array, route: PackedVector3Array, speed: float,
		altitude := 0.0, dr_algorithm := 2, start := 0.0) -> Dictionary:
	return {"marking": marking, "entity_type": PackedInt32Array(entity_type), "route": route, "speed": speed,
			"altitude": altitude, "dr_algorithm": dr_algorithm, "start": start, "surface": false}
