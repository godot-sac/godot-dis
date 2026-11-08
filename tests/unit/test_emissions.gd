extends GutTest
## Entity State Update with DisEntityManager, and the Distributed Emission Regeneration PDUs (Electromagnetic
## Emission, Designator, IFF) and Receiver beyond the golden byte comparisons: nested records, length fields,
## malformed input. Byte layouts are checked in test_pdus_golden.gd, test_captures.gd and test_wireshark.gd.

const ANKARA := [39.9334, 32.8597, 938.0]


func _origin() -> DisGeoOrigin:
	var origin := DisGeoOrigin.new()
	origin.set_origin_geodetic(ANKARA[0], ANKARA[1], ANKARA[2])
	add_child_autofree(origin)
	return origin


func _manager() -> DisEntityManager:
	var manager := DisEntityManager.new()
	manager.auto_update = false
	manager.smoothing_time = 0.0
	add_child_autofree(manager)
	return manager


func test_entity_state_update_moves_a_known_entity_and_keeps_its_type() -> void:
	var origin := _origin()
	var manager := _manager()
	var state := DisEntityStatePdu.new()
	state.entity_id = Vector3i(1, 1, 5)
	state.entity_type = PackedInt32Array([1, 1, 225, 1, 1, 3, 0])
	state.marking = "TANK-5"
	state.dr_algorithm = 2
	state.location_ecef = origin.local_to_ecef(Vector3(10, 0, -20))
	manager.handle_entity_state(state, 0.0)
	var entity: DisRemoteEntity = manager.get_entity(Vector3i(1, 1, 5))
	assert_not_null(entity)

	var update := DisEntityStateUpdatePdu.new()
	update.entity_id = Vector3i(1, 1, 5)
	update.location_ecef = origin.local_to_ecef(Vector3(110, 0, -20))
	update.linear_velocity = origin.local_vector_to_ecef(Vector3(5, 0, 0))
	update.appearance = 1 << 3
	update.timestamp = 1234
	var updated := []
	manager.entity_updated.connect(func(e): updated.append(e))
	manager.handle_entity_state_update(update, 1.0)
	assert_eq(updated.size(), 1, "entity_updated")
	assert_almost_eq(entity.position.x, 110.0, 0.01, "moved to the update's location")
	manager.process_entities(3.0)
	assert_almost_eq(entity.position.x, 120.0, 0.05, "dead reckoned with the update's velocity (DRM 2 kept)")
	var last := entity.get_last_pdu()
	assert_eq(last.marking, "TANK-5", "marking from the Entity State PDU")
	assert_eq(last.entity_type, PackedInt32Array([1, 1, 225, 1, 1, 3, 0]))
	assert_eq(last.appearance, 1 << 3)
	assert_eq(last.timestamp, 1234)
	assert_eq(state.location_ecef, origin.local_to_ecef(Vector3(10, 0, -20)), "the original PDU is not changed")


func test_entity_state_update_for_an_unknown_entity_is_ignored() -> void:
	var origin := _origin()
	var manager := _manager()
	var update := DisEntityStateUpdatePdu.new()
	update.entity_id = Vector3i(9, 9, 9)
	update.location_ecef = origin.local_to_ecef(Vector3.ZERO)
	manager.handle_entity_state_update(update, 0.0)
	assert_eq(manager.get_entity_count(), 0, "no type, no marking: nothing to create")


func test_entity_state_update_deactivated_removes() -> void:
	var origin := _origin()
	var manager := _manager()
	var state := DisEntityStatePdu.new()
	state.entity_id = Vector3i(1, 1, 6)
	state.location_ecef = origin.local_to_ecef(Vector3.ZERO)
	manager.handle_entity_state(state, 0.0)
	var update := DisEntityStateUpdatePdu.new()
	update.entity_id = Vector3i(1, 1, 6)
	update.location_ecef = state.location_ecef
	update.appearance = DisEntity.APPEARANCE_DEACTIVATED
	manager.handle_entity_state_update(update, 1.0)
	assert_eq(manager.get_entity_count(), 0)


func _emission() -> DisElectromagneticEmissionPdu:
	var pdu := DisElectromagneticEmissionPdu.new()
	pdu.emitting_entity_id = Vector3i(1, 1, 20)
	pdu.event_id = Vector3i(1, 1, 3)
	var radar := DisElectromagneticEmissionPdu.make_system()
	radar.emitter_name = 1000
	radar.emitter_function = 2
	radar.location = Vector3(0, 0, -6.5)
	var search := DisElectromagneticEmissionPdu.make_beam()
	search.beam_id = 1
	search.frequency = 3.0e9
	search.azimuth_sweep = PI
	search.track_jam = [{"entity_id": Vector3i(2, 1, 6), "emitter_number": 0, "beam_number": 0}]
	var track := DisElectromagneticEmissionPdu.make_beam()
	track.beam_id = 2
	radar.beams = [search, track]
	pdu.systems = [radar, DisElectromagneticEmissionPdu.make_system()]
	return pdu


func test_emission_round_trip_and_length_fields() -> void:
	var pdu := _emission()
	var bytes := pdu.encode()
	assert_eq(bytes.size(), 28 + 20 + 52 + 8 + 52 + 20)
	assert_eq(pdu.get_length(), bytes.size())
	# system data length and beam data length in 32-bit words
	assert_eq(bytes[28], (20 + 52 + 8 + 52) / 4)
	assert_eq(bytes[48], (52 + 8) / 4)
	var decoded := DisPdu.decode(bytes) as DisElectromagneticEmissionPdu
	assert_not_null(decoded)
	assert_eq(decoded.systems.size(), 2)
	assert_eq(decoded.systems[0].beams.size(), 2)
	assert_eq(decoded.systems[0].beams[0].track_jam[0].entity_id, Vector3i(2, 1, 6))
	assert_almost_eq(decoded.systems[0].beams[0].azimuth_sweep, PI, 1e-6)
	assert_eq(decoded.encode(), bytes, "decode + encode gives the same bytes")


func test_emission_long_system_writes_zero_length_and_still_decodes() -> void:
	var pdu := DisElectromagneticEmissionPdu.new()
	var system := DisElectromagneticEmissionPdu.make_system()
	var beam := DisElectromagneticEmissionPdu.make_beam()
	for i in 200:
		beam.track_jam.append({"entity_id": Vector3i(1, 1, i), "emitter_number": 1, "beam_number": 1})
	system.beams = [beam]
	pdu.systems = [system]
	var bytes := pdu.encode()
	assert_eq(bytes[28], 0, "more than 255 words: the system length field is 0")
	assert_eq(bytes[48], 0, "and so is the beam's")
	var decoded := DisElectromagneticEmissionPdu.decode(bytes)
	assert_eq(decoded.systems[0].beams[0].track_jam.size(), 200, "the counts drive the parsing")


func test_emission_bad_input() -> void:
	var pdu := _emission()
	var bytes := pdu.encode()
	assert_null(DisPdu.decode(bytes.slice(0, bytes.size() - 8)), "cut short")
	pdu.systems = ["not a system"]
	assert_eq(pdu.encode(), PackedByteArray(), "a system that is not a Dictionary")
	assert_engine_error("DICTIONARY")


func test_iff_layers() -> void:
	var pdu := DisIffPdu.new()
	pdu.parameters = PackedInt32Array([1, 2, 3, 4, 5, 6])
	var layer := PackedByteArray([2, 0, 0, 12, 1, 2, 3, 4, 5, 6, 7, 8])
	pdu.layers = [layer]
	var bytes := pdu.encode()
	assert_eq(bytes.size(), 60 + 12)
	var decoded := DisIffPdu.decode(bytes)
	assert_eq(decoded.layers, [layer])
	assert_eq(decoded.parameters, PackedInt32Array([1, 2, 3, 4, 5, 6]))
	# A layer that claims more bytes than there are.
	var broken := bytes.duplicate()
	broken[63] = 40
	assert_null(DisIffPdu.decode(broken))
	assert_string_contains(DisPdu.get_last_error(), "layer length")
	pdu.layers = [PackedByteArray([2, 0])]
	assert_eq(pdu.encode(), PackedByteArray(), "a layer shorter than its header")
	assert_engine_error("LAYER_HEADER_SIZE")


func test_receiver_and_designator_round_trip() -> void:
	var receiver := DisReceiverPdu.new()
	receiver.radio_reference_id = Vector3i(1, 1, 4)
	receiver.radio_number = 2
	receiver.receiver_state = DisReceiverPdu.RECEIVER_ON_AND_RECEIVING
	receiver.received_power = -80.25
	receiver.transmitter_entity_id = Vector3i(2, 1, 6)
	var bytes := receiver.encode()
	assert_eq(bytes.size(), 36)
	var back := DisPdu.decode(bytes) as DisReceiverPdu
	assert_eq(back.receiver_state, DisReceiverPdu.RECEIVER_ON_AND_RECEIVING)
	assert_eq(back.received_power, -80.25)
	assert_eq(back.transmitter_entity_id, Vector3i(2, 1, 6))

	var designator := DisDesignatorPdu.new()
	designator.designated_entity_id = Vector3i(2, 1, 6)
	designator.spot_location_ecef = PackedFloat64Array([4.0e6, 2.5e6, 4.1e6])
	designator.wavelength = 1.064
	var d_bytes := designator.encode()
	assert_eq(d_bytes.size(), 88)
	var d_back := DisPdu.decode(d_bytes) as DisDesignatorPdu
	assert_eq(d_back.spot_location_ecef, PackedFloat64Array([4.0e6, 2.5e6, 4.1e6]))
	assert_eq(d_back.encode(), d_bytes)
