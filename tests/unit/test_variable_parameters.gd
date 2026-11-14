extends GutTest
## DisVariableParameter (articulated and attached part records) against tests/golden/variable_parameters.txt
## (tools/vp_golden.py, packed with struct from the standard's layout), DisEntity publishing them and
## DisEntityManager receiving them; Wireshark's reading of the records is in test_wireshark.gd.

const GOLDEN := "res://tests/golden/variable_parameters.txt"


func _golden() -> Array:
	var out := []
	for line: String in FileAccess.get_file_as_string(GOLDEN).split("\n", false):
		if not line.begins_with("#"):
			out.append(line.split(" "))
	return out


func test_records_match_the_standard_layout() -> void:
	var n := 0
	for f: PackedStringArray in _golden():
		var expected := f[f.size() - 1].hex_decode()
		var record: PackedByteArray
		if f[0] == "articulated":
			record = DisVariableParameter.articulated_part(int(f[1]), int(f[2]), float(f[3]), int(f[4]), int(f[5]))
		else:
			var et := PackedInt32Array()
			for i in range(2, 9):
				et.append(int(f[i]))
			record = DisVariableParameter.attached_part(int(f[1]), et, int(f[9]), f[10] == "1")
		assert_eq(record, expected, " ".join(f))
		n += 1
	assert_eq(n, 8)


func test_decode() -> void:
	var a := DisVariableParameter.decode(DisVariableParameter.articulated_part(4416,
			DisVariableParameter.METRIC_ELEVATION, -0.125, 1, 3))
	assert_eq(a.record_type, DisVariableParameter.RECORD_ARTICULATED_PART)
	assert_eq([a.type_class, a.metric, a.parameter_type, a.attached_to, a.change], [4416, 13, 4429, 1, 3])
	assert_eq(a.value, -0.125)
	assert_eq(DisEnums.get_enum_name("articulated_part_class", a.type_class), "Primary Gun Number 1")
	assert_eq(DisEnums.get_enum_name("articulated_part_metric", a.metric), "Elevation")
	var b := DisVariableParameter.decode(DisVariableParameter.attached_part(641, PackedInt32Array([2, 1, 218, 1, 1, 0,
			0]), 2, true))
	assert_eq(b.record_type, DisVariableParameter.RECORD_ATTACHED_PART)
	assert_eq([b.station, b.attached_to, b.detached], [641, 2, true])
	assert_eq(b.entity_type, PackedInt32Array([2, 1, 218, 1, 1, 0, 0]))
	var other := PackedByteArray([3, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15])
	var c := DisVariableParameter.decode(other)
	assert_eq(c.record_type, 3)
	assert_eq(c.data, other.slice(1))
	assert_eq(DisVariableParameter.decode_all([other, other]).size(), 2)


func test_bad_input() -> void:
	assert_eq(DisVariableParameter.articulated_part(4097, 11, 0.0), PackedByteArray(), "class not a multiple of 32")
	assert_engine_error("multiple of 32")
	assert_eq(DisVariableParameter.articulated_part(4096, 40, 0.0), PackedByteArray(), "metric beyond 31")
	assert_engine_error("metric")
	assert_eq(DisVariableParameter.attached_part(512, PackedInt32Array([1, 2])), PackedByteArray())
	assert_engine_error("entity_type")
	assert_eq(DisVariableParameter.decode(PackedByteArray([0, 1])), {})
	assert_engine_error("16 bytes")


func test_entity_publishes_its_parts_and_the_manager_receives_them() -> void:
	var origin := DisGeoOrigin.new()
	origin.set_origin_geodetic(41.02, 29.05, 0.0)
	add_child_autofree(origin)
	var entity := DisEntity.new()
	entity.auto_update = false
	var sent: Array[DisEntityStatePdu] = []
	entity.published.connect(func(pdu: DisEntityStatePdu) -> void: sent.append(pdu))
	add_child_autofree(entity)
	var turret := DisVariableParameter.articulated_part(4096, DisVariableParameter.METRIC_AZIMUTH, 0.0)
	entity.variable_parameters = [turret]
	entity.process_entity(0.0)
	entity.process_entity(0.1)
	assert_eq(sent.size(), 1, "nothing changed")
	# Turning the turret sends a PDU at once.
	entity.variable_parameters = [DisVariableParameter.articulated_part(4096, DisVariableParameter.METRIC_AZIMUTH,
			0.8, 0, 1)]
	entity.process_entity(0.2)
	assert_eq(sent.size(), 2, "a changed part is sent at once")
	var pdu := sent[1]
	assert_eq(pdu.get_length(), 144 + 16)
	assert_eq(pdu.encode().size(), 160)
	var manager := DisEntityManager.new()
	manager.auto_update = false
	add_child_autofree(manager)
	manager.handle_entity_state(DisEntityStatePdu.decode(pdu.encode()), 0.2)
	var remote: DisRemoteEntity = manager.get_entity(pdu.entity_id)
	var parts := DisVariableParameter.decode_all(remote.get_last_pdu().variable_parameters)
	assert_eq(parts.size(), 1)
	assert_almost_eq(parts[0].value, 0.8, 1e-6)
	assert_eq(parts[0].change, 1)
