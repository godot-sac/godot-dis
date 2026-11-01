extends GutTest
## Every PDU class except Entity State (test_entity_state_pdu.gd) against reference PDUs from Open-DIS Python
## (tools/golden_pdus.py → tests/golden/pdus/). One JSON per case names the class and the expected properties.

const GOLDEN_DIR := "res://tests/golden/pdus/"
const CLASSES := ["DisFirePdu", "DisDetonationPdu", "DisRemoveEntityPdu", "DisStartResumePdu", "DisStopFreezePdu",
		"DisAcknowledgePdu"]

var _cases: Array = []


func before_all() -> void:
	_cases = JSON.parse_string(FileAccess.get_file_as_string(GOLDEN_DIR + "index.json"))


func _bytes(name: String) -> PackedByteArray:
	return FileAccess.get_file_as_bytes(GOLDEN_DIR + name + ".bin")


func _case(name: String) -> Dictionary:
	return JSON.parse_string(FileAccess.get_file_as_string(GOLDEN_DIR + name + ".json"))


static func _f64(hex: String) -> float:
	var bytes := hex.hex_decode()
	bytes.reverse()  # big-endian on disk, decode_double reads little-endian
	return bytes.decode_double(0)


## Converts a JSON value to the Godot type of the property it is expected in.
static func _to_godot(json_value: Variant, like: Variant) -> Variant:
	if json_value is Dictionary:
		if json_value.has("f64"):
			return _f64(json_value.f64)
		if json_value.has("f64_list"):
			var out := PackedFloat64Array()
			for hex: String in json_value.f64_list:
				out.append(_f64(hex))
			return out
		if json_value.has("raw_hex"):
			var out := []
			for hex: String in json_value.raw_hex:
				out.append(hex.hex_decode())
			return out
	match typeof(like):
		TYPE_INT:
			return int(json_value)
		TYPE_VECTOR3I:
			return Vector3i(json_value[0], json_value[1], json_value[2])
		TYPE_VECTOR3:
			return Vector3(json_value[0], json_value[1], json_value[2])
		TYPE_PACKED_INT32_ARRAY:
			return PackedInt32Array(json_value)
	return json_value


func _diff(pdu: Object, fields: Dictionary) -> PackedStringArray:
	var problems := PackedStringArray()
	for name: String in fields:
		var actual: Variant = pdu.get(name)
		var expected: Variant = _to_godot(fields[name], actual)
		if typeof(actual) != typeof(expected) or actual != expected:
			problems.append("%s: %s != %s" % [name, actual, expected])
	return problems


func _from_fields(class_name_: String, fields: Dictionary) -> Object:
	var pdu: Object = ClassDB.instantiate(class_name_)
	for name: String in fields:
		pdu.set(name, _to_godot(fields[name], pdu.get(name)))
	return pdu


func test_golden_cases_exist() -> void:
	assert_gt(_cases.size(), 50, "tools/golden_pdus.py output present")
	var seen := {}
	for name: String in _cases:
		seen[_case(name)["class"]] = true
	for class_name_: String in CLASSES:
		assert_true(seen.has(class_name_), class_name_ + " has golden cases")


func test_decode_matches_oracle_fields() -> void:
	var failures := PackedStringArray()
	for name: String in _cases:
		var c := _case(name)
		var pdu := DisPdu.decode(_bytes(name))
		if pdu == null:
			failures.append("%s: decode failed: %s" % [name, DisPdu.get_last_error()])
			continue
		if pdu.get_class() != c["class"]:
			failures.append("%s: decoded as %s" % [name, pdu.get_class()])
			continue
		if pdu.get_length() != int(c.length):
			failures.append("%s: length %d != %d" % [name, pdu.get_length(), c.length])
		for problem in _diff(pdu, c.fields):
			failures.append("%s: %s" % [name, problem])
	assert_eq(failures, PackedStringArray(), "every field decodes as Open-DIS Python wrote it")


func test_encode_is_byte_identical() -> void:
	var failures := PackedStringArray()
	for name: String in _cases:
		var c := _case(name)
		var bytes: PackedByteArray = _from_fields(c["class"], c.fields).encode()
		if bytes != _bytes(name):
			failures.append("%s: %s\n   != %s" % [name, bytes.hex_encode(), _bytes(name).hex_encode()])
	assert_eq(failures, PackedStringArray(), "encode() reproduces the oracle bytes")


func test_class_decode_rejects_other_types() -> void:
	var tested := {}
	for name: String in _cases:
		var c := _case(name)
		if tested.has(c["class"]):
			continue  # one case per class is enough; the type byte check is shared
		tested[c["class"]] = true
		var bytes := _bytes(name)
		for class_name_: String in CLASSES + ["DisEntityStatePdu"]:
			if class_name_ != c["class"]:
				var decoded: Variant = ClassDB.class_call_static(class_name_, "decode", bytes)
				assert_null(decoded, "%s.decode(%s)" % [class_name_, name])


func test_malformed_packets_fail_without_crashing() -> void:
	var failures := PackedStringArray()
	for name: String in _cases:
		var good := _bytes(name)
		var bad := {
			"truncated": good.slice(0, good.size() - 1),
			"header only": good.slice(0, 12),
		}
		var short_length := good.duplicate()
		short_length[8] = 0
		short_length[9] = 20
		bad["length smaller than PDU"] = short_length
		var long_length := good.duplicate()
		long_length[8] = 0xFF
		long_length[9] = 0xFF
		bad["length larger than packet"] = long_length
		if good[2] == DisPdu.PDU_TYPE_DETONATION:
			var too_many := good.duplicate()
			too_many[101] = 9
			bad["parameter count beyond packet"] = too_many
		for label: String in bad:
			if DisPdu.decode(bad[label]) != null:
				failures.append("%s / %s decoded" % [name, label])
	assert_eq(failures, PackedStringArray(), "malformed packets are rejected")


func test_random_garbage_does_not_crash() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 1279
	var types := [2, 3, 12, 13, 14, 15]
	for i in 3000:
		var bytes := PackedByteArray()
		bytes.resize(rng.randi_range(0, 200))
		for j in bytes.size():
			bytes[j] = rng.randi_range(0, 255)
		if bytes.size() > 10:
			bytes[0] = 7
			bytes[2] = types[i % types.size()]
			if rng.randf() < 0.7:
				bytes.encode_u16(8, 0)
				bytes[8] = bytes.size() >> 8
				bytes[9] = bytes.size() & 0xFF
		DisPdu.decode(bytes)
	pass_test("3000 malformed packets decoded without crashing")


func test_simulation_management_addressing() -> void:
	var pdu := DisStartResumePdu.new()
	pdu.receiving_id = Vector3i(DisSimulationManagementPdu.ID_ALL, DisSimulationManagementPdu.ID_ALL, 0)
	assert_true(pdu.is_addressed_to(3, 4), "all sites, all applications")
	pdu.receiving_id = Vector3i(3, DisSimulationManagementPdu.ID_ALL, 0)
	assert_true(pdu.is_addressed_to(3, 4), "all applications at site 3")
	assert_false(pdu.is_addressed_to(2, 4), "not site 2")
	pdu.receiving_id = Vector3i(3, 4, 17)
	assert_true(pdu.is_addressed_to(3, 4), "an entity of application 3:4")
	assert_false(pdu.is_addressed_to(3, 5))


func test_clock_time_helpers() -> void:
	var pdu := DisStartResumePdu.new()
	pdu.set_real_world_time(1793534400.25)
	assert_eq(pdu.real_world_time_hour, 498204, "hours since 1970")
	assert_eq(pdu.real_world_time_past_hour & 1, 1, "real world time is absolute")
	assert_almost_eq(pdu.get_real_world_time(), 1793534400.25, 1e-5)
	pdu.set_simulation_time(5400.125)
	assert_eq(pdu.simulation_time_hour, 1)
	assert_eq(pdu.simulation_time_past_hour & 1, 0, "simulation time is relative")
	assert_almost_eq(pdu.get_simulation_time(), 5400.125, 1e-5)
	pdu.simulation_time_hour = -1
	assert_almost_eq(pdu.get_simulation_time(), -3600.0 + DisPdu.timestamp_to_seconds(pdu.simulation_time_past_hour), 1e-5,
			"hour is signed")


func test_common_api() -> void:
	for class_name_: String in CLASSES:
		var pdu: DisPduBase = ClassDB.instantiate(class_name_)
		var bytes := pdu.encode()
		assert_eq(bytes.size(), pdu.get_length(), class_name_ + " length")
		assert_eq(bytes[2], pdu.get_pdu_type(), class_name_ + " type byte")
		assert_eq(bytes[3], pdu.get_protocol_family(), class_name_ + " family byte")
		assert_eq(DisPdu.decode(bytes).get_class(), class_name_, class_name_ + " round trip")
	assert_true(DisEntityStatePdu.new() is DisPduBase, "Entity State shares the base class")
