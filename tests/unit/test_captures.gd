extends GutTest
## Public DIS captures as regression inputs (tools/fetch_captures.py downloads them to tools/.cache/captures/;
## without them these tests are pending). Every PDU of a type we support must decode, and encode back to the same
## bytes: a byte-level cross-check against the implementations that wrote them (opendis7-java for the NPS
## AllPduSender capture; unknown DIS 7 senders for the Wireshark samples).

const CACHE := "res://tools/.cache/captures/"
## file -> [DIS datagrams, {PDU type: count} expected]
const EXPECTED := {
	"DIS_EntityState_1.pcapng": [287, {1: 287}],
	"DIS_EntityState_2.pcapng": [2, {1: 2}],
	"DIS_EnvironmentalProcess.pcapng": [395, {41: 395}],
	"DIS_Signal.pcapng": [558, {26: 558}],
	"DIS_signal_and_transmitter.pcapng": [253, {25: 10, 26: 243}],
	"AllPduSenderWiresharkCapture.pcapng": [72, {}],
}


func _load(file: String) -> DisRecording:
	var path := CACHE + file
	if not FileAccess.file_exists(path):
		return null
	var recording := DisRecording.new()
	assert_eq(recording.load(path), OK, file)
	return recording


## Decodes every supported PDU; returns problems (decode failures, re-encodings that differ).
func _check_round_trips(file: String, recording: DisRecording) -> Dictionary:
	var problems := PackedStringArray()
	var decoded := {}
	for i in recording.get_packet_count():
		var bytes := recording.get_bytes(i)
		if not DisPdu.is_supported(bytes[2]):
			continue
		var pdu := DisPdu.decode(bytes)
		if pdu == null:
			problems.append("%s #%d type %d: %s" % [file, i, bytes[2], DisPdu.get_last_error()])
			continue
		decoded[bytes[2]] = decoded.get(bytes[2], 0) + 1
		var expected := bytes.slice(0, pdu.get_length())
		if expected[8] == 0 and expected[9] == 0:
			expected.encode_u16(8, 0)  # the sender left the length 0; we write the real one
			expected[8] = pdu.get_length() >> 8
			expected[9] = pdu.get_length() & 0xFF
		if pdu.encode() != expected:
			problems.append("%s #%d %s re-encodes differently:\n  %s\n  %s" % [file, i, pdu.get_class(),
					pdu.encode().hex_encode(), expected.hex_encode()])
	return {"problems": problems, "decoded": decoded}


func test_wireshark_and_nps_captures() -> void:
	var found := 0
	for file: String in EXPECTED:
		var recording := _load(file)
		if recording == null:
			continue
		found += 1
		assert_eq(recording.get_packet_count(), EXPECTED[file][0], file + " DIS datagrams")
		var summary := recording.get_summary()
		if not EXPECTED[file][1].is_empty():
			assert_eq(summary.pdu_types, EXPECTED[file][1], file + " PDU types")
		var result := _check_round_trips(file, recording)
		assert_eq(result.problems, PackedStringArray(), file)
	if found == 0:
		pending("no captures in tools/.cache/captures (run tools/fetch_captures.py)")


func test_opendis7_java_all_pdu_sender() -> void:
	var recording := _load("AllPduSenderWiresharkCapture.pcapng")
	if recording == null:
		pending("AllPduSenderWiresharkCapture.pcapng not downloaded (tools/fetch_captures.py)")
		return
	var summary := recording.get_summary()
	assert_eq(summary.pdu_types.size(), 72, "one PDU of each of the 72 DIS 7 types")
	var result := _check_round_trips("AllPduSender", recording)
	assert_eq(result.decoded, {1: 1, 2: 1, 3: 1, 12: 1, 13: 1, 14: 1, 15: 1},
			"every type we support was found and decoded")
	assert_eq(result.problems, PackedStringArray(), "and re-encodes to the same bytes")
	var entity := DisPdu.decode(recording.get_bytes(0)) as DisEntityStatePdu
	assert_eq(entity.marking, "AllPduSende", "opendis7-java's marking, cut to 11 characters")


func test_replay_wireshark_entity_states() -> void:
	var recording := _load("DIS_EntityState_1.pcapng")
	if recording == null:
		pending("DIS_EntityState_1.pcapng not downloaded (tools/fetch_captures.py)")
		return
	var network := DisNetwork.new()
	network.autostart = false
	add_child_autofree(network)
	var origin := DisGeoOrigin.new()
	add_child_autofree(origin)
	var manager := DisEntityManager.new()
	add_child_autofree(manager)
	manager.network_path = manager.get_path_to(network)
	manager.origin_path = manager.get_path_to(origin)
	var player := DisPlayer.new()
	add_child_autofree(player)
	player.network_path = player.get_path_to(network)
	player.recording = recording
	player.advance(recording.get_duration() + 1.0)
	assert_eq(network.get_statistics().injected, 287)
	assert_eq(network.get_statistics().rejected, 0)
	assert_eq(manager.get_entity_count(), 1, "the capture's one entity is on the map")
