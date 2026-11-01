extends GutTest
## DisEntityStatePdu against reference PDUs from Open-DIS Python (tools/golden.py → tests/golden/).

const GOLDEN_DIR := "res://tests/golden/"

var _cases: Array = []


func before_all() -> void:
	var index = JSON.parse_string(FileAccess.get_file_as_string(GOLDEN_DIR + "index.json"))
	if index is Array:
		_cases = index
	elif index is Dictionary:
		_cases = index.get("cases", [])


func _bytes(name: String) -> PackedByteArray:
	return FileAccess.get_file_as_bytes(GOLDEN_DIR + name + ".bin")


func _fields(name: String) -> Dictionary:
	return JSON.parse_string(FileAccess.get_file_as_string(GOLDEN_DIR + name + ".json"))


## Location doubles come from IEEE bits, because Godot's JSON parser can be off in the last bit.
func _location(f: Dictionary) -> PackedFloat64Array:
	var out := PackedFloat64Array()
	for hex: String in f.location_ecef_bits:
		var bytes := hex.hex_decode()
		bytes.reverse()  # big-endian on disk, decode_double reads little-endian
		out.append(bytes.decode_double(0))
	return out


func _vec3(values: Array) -> Vector3:
	return Vector3(values[0], values[1], values[2])


## Returns the differences between a decoded PDU and the expected JSON fields.
func _diff_fields(pdu: DisEntityStatePdu, f: Dictionary) -> PackedStringArray:
	var problems := PackedStringArray()
	var check := func(name: String, actual: Variant, expected: Variant) -> void:
		if actual != expected:
			problems.append("%s: %s != %s" % [name, actual, expected])
	check.call("protocol_version", pdu.protocol_version, int(f.protocol_version))
	check.call("exercise_id", pdu.exercise_id, int(f.exercise_id))
	check.call("timestamp", pdu.timestamp, int(f.timestamp))
	check.call("pdu_status", pdu.pdu_status, int(f.pdu_status))
	check.call("length", pdu.get_length(), int(f.length))
	check.call("entity_id", pdu.entity_id, Vector3i(f.entity_id[0], f.entity_id[1], f.entity_id[2]))
	check.call("force_id", pdu.force_id, int(f.force_id))
	check.call("entity_type", pdu.entity_type, PackedInt32Array(f.entity_type))
	check.call("alternative_entity_type", pdu.alternative_entity_type, PackedInt32Array(f.alternative_entity_type))
	check.call("linear_velocity", pdu.linear_velocity, _vec3(f.linear_velocity))
	check.call("location_ecef", pdu.location_ecef, _location(f))
	check.call("orientation", pdu.orientation, _vec3(f.orientation))
	check.call("appearance", pdu.appearance, int(f.appearance))
	check.call("dr_algorithm", pdu.dr_algorithm, int(f.dr_algorithm))
	check.call("dr_parameters", pdu.dr_parameters, PackedByteArray(f.dr_parameters))
	check.call("dr_linear_acceleration", pdu.dr_linear_acceleration, _vec3(f.dr_linear_acceleration))
	check.call("dr_angular_velocity", pdu.dr_angular_velocity, _vec3(f.dr_angular_velocity))
	check.call("marking_character_set", pdu.marking_character_set, int(f.marking_character_set))
	check.call("marking_bytes", pdu.marking_bytes, PackedByteArray(f.marking))
	check.call("capabilities", pdu.capabilities, int(f.capabilities))
	var parameters := []
	for hex: String in f.variable_parameters:
		parameters.append(hex.hex_decode())
	check.call("variable_parameters", pdu.variable_parameters, parameters)
	return problems


## Builds a PDU from the JSON fields only (no decoding involved).
func _from_fields(f: Dictionary) -> DisEntityStatePdu:
	var pdu := DisEntityStatePdu.new()
	pdu.protocol_version = f.protocol_version
	pdu.exercise_id = f.exercise_id
	pdu.timestamp = f.timestamp
	pdu.pdu_status = f.pdu_status
	pdu.entity_id = Vector3i(f.entity_id[0], f.entity_id[1], f.entity_id[2])
	pdu.force_id = f.force_id
	pdu.entity_type = PackedInt32Array(f.entity_type)
	pdu.alternative_entity_type = PackedInt32Array(f.alternative_entity_type)
	pdu.linear_velocity = _vec3(f.linear_velocity)
	pdu.location_ecef = _location(f)
	pdu.orientation = _vec3(f.orientation)
	pdu.appearance = f.appearance
	pdu.dr_algorithm = f.dr_algorithm
	pdu.dr_parameters = PackedByteArray(f.dr_parameters)
	pdu.dr_linear_acceleration = _vec3(f.dr_linear_acceleration)
	pdu.dr_angular_velocity = _vec3(f.dr_angular_velocity)
	pdu.marking_character_set = f.marking_character_set
	pdu.marking_bytes = PackedByteArray(f.marking)
	pdu.capabilities = f.capabilities
	var parameters := []
	for hex: String in f.variable_parameters:
		parameters.append(hex.hex_decode())
	pdu.variable_parameters = parameters
	return pdu


func test_golden_set_is_loaded() -> void:
	assert_gt(_cases.size(), 20, "run tools/golden.py")


func test_decode_matches_oracle_fields() -> void:
	for name: String in _cases:
		var pdu := DisEntityStatePdu.decode(_bytes(name))
		assert_not_null(pdu, "%s: %s" % [name, DisPdu.get_last_error()])
		if pdu:
			var problems := _diff_fields(pdu, _fields(name))
			assert_eq(problems.size(), 0, "%s:\n%s" % [name, "\n".join(problems)])


func test_encode_from_fields_matches_oracle_bytes() -> void:
	for name: String in _cases:
		var expected := _bytes(name)
		var actual := _from_fields(_fields(name)).encode()
		assert_eq(actual.size(), expected.size(), name + " size")
		assert_eq(actual.hex_encode(), expected.hex_encode(), name + " bytes")


func test_decode_encode_round_trip() -> void:
	for name: String in _cases:
		var bytes := _bytes(name)
		var pdu := DisEntityStatePdu.decode(bytes)
		if pdu:
			assert_eq(pdu.encode(), bytes, name)


func test_generic_decode_returns_entity_state() -> void:
	var pdu = DisPdu.decode(_bytes(_cases[0]))
	assert_true(pdu is DisEntityStatePdu)
	var header := DisPdu.peek_header(_bytes(_cases[0]))
	assert_eq(header.pdu_type, DisPdu.PDU_TYPE_ENTITY_STATE)
	assert_eq(header.length, 144)


func test_marking_string() -> void:
	var pdu := DisEntityStatePdu.new()
	pdu.marking = "ELEVENCHARS"  # 11 characters: open-dis-cpp's string setter would drop the last one
	var decoded := DisEntityStatePdu.decode(pdu.encode())
	assert_eq(decoded.marking, "ELEVENCHARS")
	pdu.marking = "TOO LONG FOR DIS"
	assert_eq(pdu.marking, "TOO LONG FO")


func test_dis6_version_is_accepted() -> void:
	var bytes := _bytes(_cases[1])
	bytes[0] = 6
	var pdu := DisEntityStatePdu.decode(bytes)
	assert_not_null(pdu)
	assert_eq(pdu.protocol_version, 6)


func test_trailing_bytes_are_ignored() -> void:
	var bytes := _bytes(_cases[1])
	bytes.append_array(PackedByteArray([1, 2, 3, 4]))
	assert_not_null(DisEntityStatePdu.decode(bytes))


func test_malformed_packets_fail_without_crashing() -> void:
	var good := _bytes(_cases[1])
	var cases := {
		"empty": PackedByteArray(),
		"header only": good.slice(0, 12),
		"truncated body": good.slice(0, 100),
		"one byte short": good.slice(0, 143),
	}
	var wrong_type := good.duplicate()
	wrong_type[2] = 99  # no such PDU type
	cases["wrong pdu type"] = wrong_type
	var fire_type := good.duplicate()
	fire_type[2] = DisPdu.PDU_TYPE_FIRE
	assert_null(DisEntityStatePdu.decode(fire_type), "a Fire PDU is not an Entity State PDU")
	var bad_version := good.duplicate()
	bad_version[0] = 0
	cases["version 0"] = bad_version
	var long_length := good.duplicate()
	long_length.encode_u16(8, 0xFFFF)  # little-endian write: any huge value works
	cases["length larger than packet"] = long_length
	var short_length := good.duplicate()
	short_length[8] = 0
	short_length[9] = 50
	cases["length smaller than PDU"] = short_length
	var too_many_parameters := good.duplicate()
	too_many_parameters[19] = 5
	cases["parameter count beyond packet"] = too_many_parameters
	for label: String in cases:
		assert_null(DisEntityStatePdu.decode(cases[label]), label)
		assert_ne(DisPdu.get_last_error(), "", label + " sets an error")
		assert_null(DisPdu.decode(cases[label]), label + " (generic)")


func test_random_garbage_does_not_crash() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 1278
	var good := _bytes(_cases[1])
	for i in 2000:
		var bytes: PackedByteArray
		if i % 2 == 0:
			# Mutate a valid packet: flip a few bytes, sometimes truncate.
			bytes = good.duplicate()
			for j in rng.randi_range(1, 6):
				bytes[rng.randi_range(0, bytes.size() - 1)] = rng.randi_range(0, 255)
			if rng.randf() < 0.3:
				bytes = bytes.slice(0, rng.randi_range(0, bytes.size()))
		else:
			bytes.resize(rng.randi_range(0, 400))
			for j in bytes.size():
				bytes[j] = rng.randi_range(0, 255)
			if bytes.size() > 2 and rng.randf() < 0.5:
				bytes[2] = 1  # pretend to be an Entity State PDU
		DisPdu.decode(bytes)
	pass_test("2000 malformed packets decoded without crashing")


func test_timestamps() -> void:
	var absolute := DisPdu.timestamp_from_seconds(1800.0, true)
	assert_true(DisPdu.timestamp_is_absolute(absolute))
	assert_eq(absolute, (1 << 31) | 1, "half an hour is 2^30 units, shifted left by one")
	assert_almost_eq(DisPdu.timestamp_to_seconds(absolute), 1800.0, 1e-5)
	var relative := DisPdu.timestamp_from_seconds(3600.0 + 12.5, false)
	assert_false(DisPdu.timestamp_is_absolute(relative))
	assert_almost_eq(DisPdu.timestamp_to_seconds(relative), 12.5, 1e-5, "wraps at one hour")
	assert_almost_eq(DisPdu.timestamp_to_seconds(DisPdu.timestamp_from_seconds(3599.9999, false)), 3599.9999, 1e-5)
