extends GutTest
## DisGeo and DisGeoOrigin through the compiled extension. The C++ math itself is tested against
## pymap3d / Open-DIS Python in tests/cpp; here we check the Godot side: units, axes, Basis, float32 limits.

const GOLDEN := "res://tests/golden/geo_cases.txt"
const ANKARA := [39.9334, 32.8597, 938.0]


func _golden(kind: String) -> Array:
	var cases := []
	for line in FileAccess.get_file_as_string(GOLDEN).split("\n", false):
		var parts := line.split(" ", false)
		if parts[0] != kind:
			continue
		var values := PackedFloat64Array()
		for i in range(1, parts.size()):
			values.append(parts[i].to_float())
		cases.append(values)
	return cases


func _origin(lat: float, lon: float, height: float) -> DisGeoOrigin:
	var origin := DisGeoOrigin.new()
	origin.set_origin_geodetic(lat, lon, height)
	add_child_autofree(origin)
	return origin


func _distance(a: PackedFloat64Array, b: PackedFloat64Array) -> float:
	return Vector3(a[0] - b[0], a[1] - b[1], a[2] - b[2]).length()


func _basis_diff(a: Basis, b: Basis) -> float:
	return maxf((a.x - b.x).length(), maxf((a.y - b.y).length(), (a.z - b.z).length()))


func test_geodetic_ecef_matches_pymap3d() -> void:
	var cases := _golden("geo")
	assert_gt(cases.size(), 50)
	for c: PackedFloat64Array in cases:
		var ecef := DisGeo.geodetic_to_ecef(c[0], c[1], c[2])
		assert_lt(_distance(ecef, PackedFloat64Array([c[3], c[4], c[5]])), 1e-6, "%s" % c)
		var back := DisGeo.ecef_to_geodetic(PackedFloat64Array([c[3], c[4], c[5]]))
		assert_almost_eq(back[0], c[0], 1e-9, "latitude %s" % c)
		if absf(c[0]) < 90.0:
			assert_almost_eq(wrapf(back[1] - c[1], -180.0, 180.0), 0.0, 1e-9, "longitude %s" % c)
		assert_almost_eq(back[2], c[2], 1e-6, "height %s" % c)


func test_local_positions_match_pymap3d_enu() -> void:
	var checked := 0
	for c: PackedFloat64Array in _golden("enu"):
		if c[0] != ANKARA[0]:
			continue
		var origin := _origin(c[0], c[1], c[2])
		var local := origin.geodetic_to_local(c[3], c[4], c[5])
		# Godot axes: x = east, y = up, z = -north. Vector3 is 32-bit: ~1.5 cm at 200 km.
		var expected := Vector3(c[6], c[8], -c[7])
		assert_lt(local.distance_to(expected), 1e-3 + expected.length() * 1e-7, "%s" % c)
		var ecef := DisGeo.geodetic_to_ecef(c[3], c[4], c[5])
		assert_lt(origin.ecef_to_local(ecef).distance_to(expected), 1e-3 + expected.length() * 1e-7)
		assert_lt(_distance(origin.local_to_ecef(local), ecef), 0.02, "back to ECEF %s" % c)
		origin.free()
		checked += 1
	assert_gt(checked, 10)


func test_origin_axes() -> void:
	var origin := _origin(ANKARA[0], ANKARA[1], ANKARA[2])
	assert_eq(origin.ecef_to_local(origin.get_origin_ecef()), Vector3.ZERO)
	assert_eq(origin.latitude, ANKARA[0])
	var north := origin.geodetic_to_local(ANKARA[0] + 0.01, ANKARA[1], ANKARA[2])
	assert_lt(north.z, -1000.0, "north is -Z")
	assert_almost_eq(north.x, 0.0, 0.01)
	var east := origin.geodetic_to_local(ANKARA[0], ANKARA[1] + 0.01, ANKARA[2])
	assert_gt(east.x, 800.0, "east is +X")
	var up := origin.geodetic_to_local(ANKARA[0], ANKARA[1], ANKARA[2] + 100.0)
	assert_almost_eq(up, Vector3(0, 100, 0), Vector3.ONE * 1e-6)
	var geodetic := origin.local_to_geodetic(Vector3(0, 100, 0))
	assert_almost_eq(geodetic[2], ANKARA[2] + 100.0, 1e-6)
	assert_almost_eq(geodetic[0], ANKARA[0], 1e-9)


func test_origin_is_in_group() -> void:
	var origin := _origin(0, 0, 0)
	assert_eq(get_tree().get_first_node_in_group("dis_geo_origin"), origin)


func test_level_north_facing_entity_is_identity_basis() -> void:
	var origin := _origin(ANKARA[0], ANKARA[1], ANKARA[2])
	var dis := DisGeo.heading_pitch_roll_to_dis_euler(Vector3.ZERO, ANKARA[0], ANKARA[1])
	assert_lt(_basis_diff(origin.dis_euler_to_basis(dis), Basis.IDENTITY), 1e-6)


## At the origin, heading / pitch / roll must give the same basis as Godot's own Euler rotation:
## yaw about +Y (heading is clockwise, so negated), pitch about +X, roll about the forward (-Z) axis.
func test_basis_matches_godot_euler_at_origin() -> void:
	var origin := _origin(ANKARA[0], ANKARA[1], ANKARA[2])
	var rng := RandomNumberGenerator.new()
	rng.seed = 1278
	for i in 200:
		var hpr := Vector3(rng.randf_range(-PI, PI), rng.randf_range(-1.5, 1.5), rng.randf_range(-PI, PI))
		var dis := DisGeo.heading_pitch_roll_to_dis_euler(hpr, ANKARA[0], ANKARA[1])
		var expected := Basis.from_euler(Vector3(hpr.y, -hpr.x, -hpr.z))  # YXZ order
		assert_lt(_basis_diff(origin.dis_euler_to_basis(dis), expected), 1e-5, "hpr %s" % hpr)


func test_heading_pitch_roll_directions() -> void:
	var origin := _origin(ANKARA[0], ANKARA[1], ANKARA[2])
	var basis_for := func(heading_deg: float, pitch_deg: float, roll_deg: float) -> Basis:
		var hpr := Vector3(deg_to_rad(heading_deg), deg_to_rad(pitch_deg), deg_to_rad(roll_deg))
		return origin.dis_euler_to_basis(DisGeo.heading_pitch_roll_to_dis_euler(hpr, ANKARA[0], ANKARA[1]))
	var east: Basis = basis_for.call(90, 0, 0)
	assert_almost_eq(-east.z, Vector3.RIGHT, Vector3.ONE * 1e-6, "heading 90° looks east (+X)")
	var climbing: Basis = basis_for.call(0, 10, 0)
	assert_almost_eq(-climbing.z, Vector3(0, sin(deg_to_rad(10)), -cos(deg_to_rad(10))), Vector3.ONE * 1e-6)
	var rolled: Basis = basis_for.call(0, 0, 30)
	assert_lt(rolled.x.y, -0.49, "positive roll lowers the right wing")


func test_basis_round_trip() -> void:
	var origin := _origin(-33.8688, 151.2093, 58.0)
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	for i in 200:
		var dis := Vector3(rng.randf_range(-PI, PI), rng.randf_range(-1.5, 1.5), rng.randf_range(-PI, PI))
		var basis := origin.dis_euler_to_basis(dis)
		assert_lt(_basis_diff(origin.dis_euler_to_basis(origin.basis_to_dis_euler(basis)), basis), 1e-5)
	# Scale is ignored.
	var scaled := origin.dis_euler_to_basis(Vector3(0.3, 0.2, 0.1)).scaled(Vector3(2, 3, 4))
	var unscaled := origin.dis_euler_to_basis(origin.basis_to_dis_euler(scaled))
	assert_lt(_basis_diff(unscaled, scaled.orthonormalized()), 1e-5)


func test_heading_pitch_roll_round_trip_and_oracle() -> void:
	for c: PackedFloat64Array in _golden("euler"):
		var hpr := Vector3(c[2], c[3], c[4])
		var dis := DisGeo.heading_pitch_roll_to_dis_euler(hpr, c[0], c[1])
		# Same rotation as Open-DIS Python (compared as bases, the angle triples may differ).
		var origin_free := DisGeoOrigin.new()
		origin_free.set_origin_geodetic(c[0], c[1], 0.0)
		var expected := origin_free.dis_euler_to_basis(Vector3(c[5], c[6], c[7]))
		assert_lt(_basis_diff(origin_free.dis_euler_to_basis(dis), expected), 1e-5, "%s" % c)
		var back := DisGeo.dis_euler_to_heading_pitch_roll(dis, c[0], c[1])
		assert_lt(_basis_diff(Basis.from_euler(Vector3(back.y, -back.x, -back.z)),
				Basis.from_euler(Vector3(hpr.y, -hpr.x, -hpr.z))), 1e-5)
		origin_free.free()


func test_vectors() -> void:
	var origin := _origin(ANKARA[0], ANKARA[1], ANKARA[2])
	var v := Vector3(12.5, -3.0, 40.0)
	assert_almost_eq(origin.ecef_vector_to_local(origin.local_vector_to_ecef(v)), v, Vector3.ONE * 1e-5)
	# Local up in ECEF is the ellipsoid normal: (cos lat cos lon, cos lat sin lon, sin lat).
	var lat := deg_to_rad(ANKARA[0])
	var lon := deg_to_rad(ANKARA[1])
	var normal := Vector3(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat))
	assert_almost_eq(origin.local_vector_to_ecef(Vector3.UP), normal, Vector3.ONE * 1e-6)


func test_entity_state_pdu_to_transform() -> void:
	var origin := _origin(ANKARA[0], ANKARA[1], ANKARA[2])
	var pdu := DisEntityStatePdu.new()
	pdu.location_ecef = DisGeo.geodetic_to_ecef(ANKARA[0] + 0.001, ANKARA[1], ANKARA[2] + 50.0)
	pdu.orientation = DisGeo.heading_pitch_roll_to_dis_euler(Vector3(deg_to_rad(90), 0, 0), ANKARA[0], ANKARA[1])
	var decoded := DisEntityStatePdu.decode(pdu.encode())  # orientation goes through float32 on the wire
	var transform := origin.dis_to_transform(decoded.location_ecef, decoded.orientation)
	assert_almost_eq(transform.origin.z, -111.0, 0.5, "about 111 m north")
	assert_almost_eq(transform.origin.y, 50.0, 0.01, "50 m up (tangent plane drop is mm here)")
	assert_almost_eq(-transform.basis.z, Vector3.RIGHT, Vector3.ONE * 1e-5, "facing east")
