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
		if row["_ws.malformed"] != "":
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
