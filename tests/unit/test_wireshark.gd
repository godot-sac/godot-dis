extends GutTest
## Our pcapng files decoded by Wireshark's DIS dissector (tshark, used as an outside tool; pending if not installed):
## every golden PDU, encoded by godot-dis and written by DisRecording, must come out as the right PDU type with the
## right fields and without "malformed" warnings.

const TSHARK_PATHS: PackedStringArray = ["C:/Program Files/Wireshark/tshark.exe", "/usr/bin/tshark",
		"/usr/local/bin/tshark", "/opt/homebrew/bin/tshark", "/Applications/Wireshark.app/Contents/MacOS/tshark"]
const GOLDEN := "res://tests/golden/pdus/"


func _tshark() -> String:
	for path in TSHARK_PATHS:
		if FileAccess.file_exists(path):
			return path
	return ""


func _golden_pdus() -> Array:
	var names: Array = JSON.parse_string(FileAccess.get_file_as_string(GOLDEN + "index.json"))
	var out := []
	for name: String in names:
		out.append(DisPdu.decode(FileAccess.get_file_as_bytes(GOLDEN + name + ".bin")))
	for name: String in JSON.parse_string(FileAccess.get_file_as_string("res://tests/golden/index.json")):
		out.append(DisPdu.decode(FileAccess.get_file_as_bytes("res://tests/golden/" + name + ".bin")))
	return out


## Wireshark 4.6 raises an exception on a Signal PDU without data (data length 0), although the PDU is complete
## (Open-DIS Python writes the same bytes). See docs/interoperability.md.
static func _known_wireshark_limit(pdu: DisPduBase) -> bool:
	return pdu is DisSignalPdu and pdu.data_length == 0


## The fields this tshark knows, out of `fields` (older Wireshark versions lack some DIS fields).
func _missing_fields(tshark: String, fields: PackedStringArray) -> PackedStringArray:
	var output := []
	OS.execute(tshark, ["-G", "fields"], output, true)
	var known := {}
	for line: String in "".join(output).split("
", false):
		var columns := line.split("	")
		if columns.size() > 2:
			known[columns[2]] = true
	var missing := PackedStringArray()
	for field in fields:
		if not field.begins_with("_ws.") and not known.has(field):
			missing.append(field)
	return missing


## Runs tshark on `file` and returns one Dictionary per frame with the requested fields.
func _tshark_fields(tshark: String, file: String, fields: PackedStringArray) -> Array:
	var args := PackedStringArray(["-r", ProjectSettings.globalize_path(file), "-d", "udp.port==3000,dis", "-T",
			"fields", "-E", "separator=|", "-E", "occurrence=a", "-E", "aggregator=,"])
	for field in fields:
		args.append_array(["-e", field])
	var output := []
	var code := OS.execute(tshark, args, output, true)
	assert_eq(code, 0, "tshark ran: %s" % "\n".join(output))
	var rows := []
	for line: String in "".join(output).split("\n", false):
		var values := line.strip_edges(false, true).split("|")
		var row := {}
		for i in fields.size():
			row[fields[i]] = values[i] if i < values.size() else ""
		rows.append(row)
	return rows


func test_wireshark_decodes_our_pcapng() -> void:
	var tshark := _tshark()
	if tshark.is_empty():
		pending("tshark not found (Wireshark not installed)")
		return
	var pdus := _golden_pdus()
	var recording := DisRecording.new()
	var t := 1793534400.0
	for pdu: DisPduBase in pdus:
		t += 0.01
		recording.add_packet(pdu.encode(), t, "192.168.1.20", 3000, "239.1.2.3", 3000)
	var file := "user://wireshark_check.pcapng"
	assert_eq(recording.save(file), OK)
	var rows := _tshark_fields(tshark, file, ["frame.number", "dis.pdu_type", "dis.pdu_length", "dis.entity_id_site",
			"_ws.malformed", "_ws.expert.message", "_ws.expert.severity"])
	assert_eq(rows.size(), pdus.size(), "one frame per PDU")
	var problems := PackedStringArray()
	var severities := {}
	for i in mini(rows.size(), pdus.size()):
		var pdu: DisPduBase = pdus[i]
		var row: Dictionary = rows[i]
		if row["dis.pdu_type"] != str(pdu.get_pdu_type()):
			problems.append("frame %d: %s seen as PDU type '%s'" % [i + 1, pdu.get_class(), row["dis.pdu_type"]])
		if row["dis.pdu_length"] != str(pdu.get_length()):
			problems.append("frame %d: length %s != %d" % [i + 1, row["dis.pdu_length"], pdu.get_length()])
		if row["_ws.malformed"] != "" and not _known_wireshark_limit(pdu):
			problems.append("frame %d: %s malformed: %s" % [i + 1, pdu.get_class(), row["_ws.expert.message"]])
		if row["_ws.expert.message"] != "":
			severities[pdu.get_class()] = row["_ws.expert.message"]
	assert_eq(problems, PackedStringArray(), "Wireshark reads every PDU as written")
	# Expert notes about field values (e.g. random enumerations in the random golden cases) are allowed but listed.
	if not severities.is_empty():
		gut.p("Wireshark notes (not failures): %s" % severities)


func test_wireshark_reads_entity_state_fields() -> void:
	var tshark := _tshark()
	if tshark.is_empty():
		pending("tshark not found (Wireshark not installed)")
		return
	var origin := DisGeoOrigin.new()
	add_child_autofree(origin)
	origin.set_origin_geodetic(39.9334, 32.8597, 938.0)
	var pdu := DisEntityStatePdu.new()
	pdu.entity_id = Vector3i(1, 2, 300)
	pdu.force_id = 1
	pdu.entity_type = PackedInt32Array([1, 1, 218, 1, 2, 0, 0])
	pdu.marking = "TANK-01"
	pdu.location_ecef = origin.local_to_ecef(Vector3(100, 0, -200))
	var fire := DisFirePdu.new()
	fire.firing_entity_id = Vector3i(1, 2, 300)
	fire.target_entity_id = Vector3i(2, 1, 7)
	fire.event_id = Vector3i(1, 2, 42)
	fire.range = 2500.0
	var recording := DisRecording.new()
	recording.add_packet(pdu.encode(), 1793534400.0)
	recording.add_packet(fire.encode(), 1793534400.5)
	recording.save("user://wireshark_fields.pcapng")
	var rows := _tshark_fields(tshark, "user://wireshark_fields.pcapng", ["dis.entity_id_site",
			"dis.entity_id_application", "dis.entity_id_entity", "dis.entity_marking", "dis.entityKind",
			"dis.country", "dis.event_number", "dis.range"])
	assert_eq(rows.size(), 2)
	assert_eq(rows[0]["dis.entity_marking"], "TANK-01")
	assert_string_contains(rows[0]["dis.entity_id_entity"], "300")
	assert_string_contains(rows[0]["dis.country"], "218")
	assert_string_contains(rows[1]["dis.event_number"], "42")
	assert_string_contains(rows[1]["dis.range"], "2500")


## Receiver and IFF have no Open-DIS Python oracle (tools/golden_pdus.py packs them from the standard), so check
## Wireshark's reading of their fields; Designator and Electromagnetic Emission too.
func test_wireshark_reads_emission_and_receiver_fields() -> void:
	var tshark := _tshark()
	if tshark.is_empty():
		pending("tshark not found (Wireshark not installed)")
		return
	var receiver := DisReceiverPdu.new()
	receiver.radio_reference_id = Vector3i(1, 1, 4)
	receiver.radio_number = 3
	receiver.receiver_state = DisReceiverPdu.RECEIVER_ON_AND_RECEIVING
	receiver.received_power = -72.5
	receiver.transmitter_entity_id = Vector3i(2, 1, 6)
	receiver.transmitter_radio_number = 5
	var iff := DisIffPdu.new()
	iff.emitting_entity_id = Vector3i(1, 1, 4)
	iff.system_type = 1
	iff.system_name = 2
	iff.system_mode = 2
	iff.system_designator = 7
	iff.system_specific_data = 9
	iff.data_field_1 = 3
	iff.information_layers = 1
	iff.parameters = PackedInt32Array([11, 22, 33, 44, 55, 66])
	var designator := DisDesignatorPdu.new()
	designator.code_name = 5
	designator.designator_code = 1
	designator.power = 10.5
	designator.wavelength = 1.064
	var emission := DisElectromagneticEmissionPdu.new()
	var system := DisElectromagneticEmissionPdu.make_system()
	var beam := DisElectromagneticEmissionPdu.make_beam()
	beam.frequency = 9.5e9
	beam.parameter_index = 77
	beam.jamming_technique = 0x01020304
	beam.track_jam = [{"entity_id": Vector3i(2, 1, 6), "emitter_number": 1, "beam_number": 2}]
	system.beams = [beam]
	emission.systems = [system]
	var recording := DisRecording.new()
	for pdu: DisPduBase in [receiver, iff, designator, emission]:
		recording.add_packet(pdu.encode(), 1793534400.0 + recording.get_packet_count())
	recording.save("user://wireshark_emission_fields.pcapng")
	var fields := PackedStringArray(["dis.radio.receive_state",
			"dis.receive_power", "dis.radio.transmitter_radio_id", "dis.iff.system_type", "dis.iff.system_name",
			"dis.iff.system_mode", "dis.iff.system_designator", "dis.iff.system_specific_data",
			"dis.iff.alternate_parameter_4", "dis.iff.parameter_1", "dis.iff.parameter_6", "dis.designator_code_name",
			"dis.designator_power", "dis.designator_wavelength", "dis.em.fund.frequency", "dis.em.beam.parameter_index",
			"dis.jamming_mode_seq", "_ws.malformed"])
	var missing := _missing_fields(tshark, fields)
	if not missing.is_empty():
		pending("this tshark has no %s (Wireshark 4.6 has them)" % ", ".join(missing))
		return
	var rows := _tshark_fields(tshark, "user://wireshark_emission_fields.pcapng", fields)
	assert_eq(rows.size(), 4)
	if rows.size() != 4:
		return
	assert_eq(rows[0]["dis.radio.receive_state"], "2")
	assert_eq(rows[0]["dis.receive_power"], "-72.5")
	assert_eq(rows[0]["dis.radio.transmitter_radio_id"], "5")
	for field: String in ["dis.iff.system_type", "dis.iff.system_name", "dis.iff.system_mode"]:
		assert_ne(rows[1][field], "", field)
	assert_eq(rows[1]["dis.iff.system_designator"], "7")
	assert_eq(rows[1]["dis.iff.system_specific_data"], "9")
	assert_eq(rows[1]["dis.iff.alternate_parameter_4"], "3")
	assert_eq(rows[1]["dis.iff.parameter_1"], "11")
	assert_eq(rows[1]["dis.iff.parameter_6"], "66")
	assert_eq(rows[2]["dis.designator_code_name"], "5")
	assert_eq(rows[2]["dis.designator_power"], "10.5")
	assert_string_contains(rows[2]["dis.designator_wavelength"], "1.064")
	assert_eq(rows[3]["dis.em.beam.parameter_index"], "77")
	assert_almost_eq(float(rows[3]["dis.em.fund.frequency"]), 9.5e9, 1000.0, "float32 on the wire")
	assert_eq(rows[3]["dis.jamming_mode_seq"], str(0x01020304))
	for row: Dictionary in rows:
		assert_eq(row["_ws.malformed"], "")


## Articulated and attached part records (DisVariableParameter, packed from the standard's layout): Wireshark must
## read the same parameter type, value, change indicator and attached part.
func test_wireshark_reads_variable_parameters() -> void:
	var tshark := _tshark()
	if tshark.is_empty():
		pending("tshark not found (Wireshark not installed)")
		return
	var fields := PackedStringArray(["dis.num_articulation_params", "dis.variable_parameter_type",
			"dis.vp.change", "dis.vp.part_attached_to_id", "dis.vp.artic_param_type",
			"dis.vp.parameter_value"])
	var missing := _missing_fields(tshark, fields)
	if not missing.is_empty():
		pending("this tshark lacks %s" % ", ".join(missing))
		return
	var pdu := DisEntityStatePdu.new()
	pdu.entity_id = Vector3i(1, 2, 3)
	pdu.entity_type = PackedInt32Array([1, 1, 218, 1, 2, 0, 0])
	pdu.location_ecef = PackedFloat64Array([4208000.0, 2335000.0, 4170000.0])
	pdu.variable_parameters = [
		DisVariableParameter.articulated_part(4096, DisVariableParameter.METRIC_AZIMUTH, 0.5, 0, 7),
		DisVariableParameter.articulated_part(4416, DisVariableParameter.METRIC_ELEVATION, -0.125, 1, 2),
		DisVariableParameter.attached_part(512, PackedInt32Array([2, 2, 225, 1, 2, 3, 0])),
	]
	var recording := DisRecording.new()
	recording.add_packet(pdu.encode(), 1793534400.0)
	recording.save("user://wireshark_vp.pcapng")
	var rows := _tshark_fields(tshark, "user://wireshark_vp.pcapng", fields)
	assert_eq(rows.size(), 1)
	var row: Dictionary = rows[0]
	assert_eq(row["dis.num_articulation_params"], "3")
	assert_eq(Array(row["dis.variable_parameter_type"].split(",")), ["0", "0", "1"], "record types")
	assert_eq(Array(row["dis.vp.change"].split(",")), ["7", "2"], "change indicators")
	assert_string_contains(row["dis.vp.artic_param_type"], "4107")
	assert_string_contains(row["dis.vp.artic_param_type"], "4429")
	var values := Array(row["dis.vp.parameter_value"].split(",")).map(func(v: String) -> float: return float(v))
	assert_almost_eq(values[0], 0.5, 1e-6)
	assert_almost_eq(values[1], -0.125, 1e-6)
