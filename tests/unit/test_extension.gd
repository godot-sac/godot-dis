extends GutTest
## Phase 0 smoke test: the GDExtension loads and open-dis-cpp encodes an Entity State PDU.


func test_extension_classes_are_registered() -> void:
	assert_true(ClassDB.class_exists("DisProbe"))
	assert_string_contains(DisProbe.get_version(), "open-dis-cpp")


func test_entity_state_pdu_header() -> void:
	var bytes := DisProbe.encode_test_entity_state()
	assert_eq(bytes.size(), 144, "Entity State PDU without variable parameters")
	assert_eq(bytes[0], 7, "protocol version DIS 7")
	assert_eq(bytes[1], 1, "exercise ID")
	assert_eq(bytes[2], 1, "PDU type: Entity State")
	assert_eq(bytes[3], 1, "protocol family: Entity Information")
	assert_eq(bytes.decode_u16(8), 0x9000, "length 144 big-endian, read little-endian")


func test_entity_state_pdu_fields_are_big_endian() -> void:
	var bytes := DisProbe.encode_test_entity_state()
	var stream := StreamPeerBuffer.new()
	stream.big_endian = true
	stream.data_array = bytes
	stream.seek(12)
	assert_eq(stream.get_u16(), 1, "site")
	assert_eq(stream.get_u16(), 2, "application")
	assert_eq(stream.get_u16(), 3, "entity")
	stream.seek(48)
	assert_eq(stream.get_double(), 4000000.5, "ECEF X")
	assert_eq(stream.get_double(), 3000000.25, "ECEF Y")
	assert_eq(stream.get_double(), 2000000.125, "ECEF Z")
	stream.seek(128)
	assert_eq(stream.get_u8(), 1, "marking character set ASCII")
	assert_eq(bytes.slice(129, 134).get_string_from_ascii(), "GODOT")
