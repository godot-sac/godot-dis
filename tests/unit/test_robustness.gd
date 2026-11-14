extends GutTest
## Pre-release review findings: signal handlers that change what is being walked, bundled and malformed input,
## request IDs from different managers, body-axis dead reckoning. Ports 317x.

const ALL := DisSimulationManagementPdu.ID_ALL
const DIR := "user://test_robustness"

var _origin: DisGeoOrigin


func before_each() -> void:
	_origin = DisGeoOrigin.new()
	_origin.set_origin_geodetic(41.02, 29.05, 0.0)
	add_child_autofree(_origin)


func _network(port: int, destination_port := 0, autostart := true) -> DisNetwork:
	var network := DisNetwork.new()
	network.mode = DisNetwork.MODE_UNICAST
	network.port = port
	network.destination_address = "127.0.0.1"
	network.destination_port = destination_port
	network.autostart = autostart
	add_child_autofree(network)
	return network


func _manager() -> DisEntityManager:
	var manager := DisEntityManager.new()
	manager.smoothing_time = 0.0
	add_child_autofree(manager)
	manager.origin_path = manager.get_path_to(_origin)
	return manager


func _entity_state(n: int, position := Vector3.ZERO) -> DisEntityStatePdu:
	var pdu := DisEntityStatePdu.new()
	pdu.entity_id = Vector3i(7, 1, n)
	pdu.entity_type = PackedInt32Array([1, 1, 225, 1, 1, 0, 0])
	pdu.location_ecef = _origin.local_to_ecef(position)
	pdu.dr_algorithm = 2
	return pdu


func _wait_for(condition: Callable, seconds: float) -> bool:
	var deadline := Time.get_ticks_msec() + int(seconds * 1000)
	while not condition.call() and Time.get_ticks_msec() < deadline:
		await wait_process_frames(1)
	return condition.call()


func test_handler_stops_the_network_while_packets_wait() -> void:
	var receiver := _network(3170)
	var sender := _network(3171, 3170)
	var received := []
	receiver.entity_state_received.connect(func(pdu):
		received.append(pdu)
		receiver.stop())
	for i in 3:
		sender.send_pdu(_entity_state(i + 1))
	await wait_seconds(0.3)
	receiver.poll()  # all three are waiting; the first handler stops the network
	assert_eq(received.size(), 1, "no packet is read after stop()")
	assert_false(receiver.is_running())


func test_bundled_pdus_in_one_datagram() -> void:
	var network := _network(3172, 0, false)
	var received := []
	network.entity_state_received.connect(func(pdu): received.append(pdu.entity_id))
	var fires := []
	network.fire_received.connect(func(pdu): fires.append(pdu))
	var fire := DisFirePdu.new()
	fire.firing_entity_id = Vector3i(7, 1, 1)
	var datagram := _entity_state(1).encode() + fire.encode() + _entity_state(2).encode()
	network.inject_packet(datagram, "127.0.0.1", 3000, false)
	assert_eq(received, [Vector3i(7, 1, 1), Vector3i(7, 1, 2)], "both Entity State PDUs")
	assert_eq(fires.size(), 1, "and the Fire PDU between them")
	received.clear()
	var single := _entity_state(3).encode()
	single.append_array(PackedByteArray([0, 0, 0, 0, 0, 0, 0, 0]))  # padding, not another PDU
	network.inject_packet(single, "127.0.0.1", 3000, false)
	assert_eq(received, [Vector3i(7, 1, 3)], "bytes after the PDU that are no PDU header change nothing")


func test_variable_parameters_must_fit_the_pdu_length() -> void:
	var bytes := _entity_state(1).encode()
	bytes[19] = 2  # two variable parameters, but the length field still says 144
	var extra := PackedByteArray()
	extra.resize(32)
	extra.fill(0x55)
	bytes.append_array(extra)
	assert_null(DisEntityStatePdu.decode(bytes), "the 32 bytes after the PDU are not its parameters")


func test_non_finite_entity_state_is_ignored() -> void:
	var manager := _manager()
	var pdu := _entity_state(1)
	pdu.location_ecef = PackedFloat64Array([NAN, 0.0, 0.0])
	manager.handle_entity_state(pdu)
	pdu = _entity_state(2)
	pdu.linear_velocity = Vector3(INF, 0, 0)
	manager.handle_entity_state(pdu)
	assert_eq(manager.get_entity_count(), 0)
	assert_push_warning_count(1, "warned once")
	manager.handle_entity_state(_entity_state(3))
	assert_eq(manager.get_entity_count(), 1)


func test_requests_from_different_managers_with_the_same_id() -> void:
	var network := _network(3173, 3174)
	network.site_id = 5
	var control := DisExerciseControl.new()
	add_child_autofree(control)
	control.network_path = control.get_path_to(network)
	var stop := func(from: Vector3i, request_id: int):
		var pdu := DisStopFreezePdu.new()
		pdu.originating_id = from
		pdu.receiving_id = Vector3i(ALL, ALL, ALL)
		pdu.request_id = request_id
		control.handle_pdu(pdu)
	var start := DisStartResumePdu.new()
	start.originating_id = Vector3i(2, 1, 0)
	start.receiving_id = Vector3i(ALL, ALL, ALL)
	start.request_id = 6
	stop.call(Vector3i(2, 1, 0), 5)
	assert_false(control.is_running())
	control.handle_pdu(start)
	assert_true(control.is_running())
	# Site 0, application 0x201 used to share a key with site 2, application 1.
	stop.call(Vector3i(0, 0x201, 0), 5)
	assert_false(control.is_running(), "another manager's request with the same ID is a new request")
	control.handle_pdu(start)
	assert_false(control.is_running(), "the same request again is a resend and changes nothing")


func test_player_handler_replaces_or_clears_the_recording() -> void:
	var network := _network(3175, 0, false)
	var player := DisPlayer.new()
	add_child_autofree(player)
	player.network_path = player.get_path_to(network)
	var t0 := 1793534400.0
	for action in ["replace", "clear"]:
		var recording := DisRecording.new()
		for i in 5:
			recording.add_packet(_entity_state(1, Vector3(i, 0, 0)).encode(), t0 + i * 0.1, "10.0.0.7", 3000,
					"239.1.2.3", 3000)
		player.recording = recording
		var played := []
		var handler := func(index, _bytes):
			played.append(index)
			if action == "replace":
				player.recording = DisRecording.new()
			else:
				player.recording.clear()
		player.packet_played.connect(handler)
		player.advance(1.0)
		player.packet_played.disconnect(handler)
		assert_eq(played, [0], "%s: playback stops at the changed recording" % action)


func test_radio_handler_clears_the_monitor() -> void:
	var monitor := DisRadioMonitor.new()
	monitor.timeout = 0.05
	add_child_autofree(monitor)
	var events := []
	var clear_on_add := func(_id, _radio):
		events.append("added")
		monitor.clear()
	monitor.radio_added.connect(clear_on_add)
	monitor.transmission_started.connect(func(_id, _radio, _frequency): events.append("started"))
	var pdu := DisTransmitterPdu.new()
	pdu.radio_reference_id = Vector3i(1, 1, 4)
	pdu.radio_number = 1
	pdu.transmit_state = DisTransmitterPdu.TRANSMIT_ON_AND_TRANSMITTING
	monitor.handle_pdu(pdu)
	assert_eq(events, ["added", "started"], "events of one PDU arrive in order, after the monitor is updated")
	assert_eq(monitor.get_radios().size(), 0, "the handler's clear() holds")

	monitor.radio_added.disconnect(clear_on_add)
	monitor.transmission_ended.connect(func(_id, _radio): monitor.clear())
	monitor.handle_pdu(pdu)
	var second := DisTransmitterPdu.new()
	second.radio_reference_id = Vector3i(1, 1, 4)
	second.radio_number = 2
	second.transmit_state = DisTransmitterPdu.TRANSMIT_ON_AND_TRANSMITTING
	monitor.handle_pdu(second)
	await wait_seconds(0.1)
	monitor.update()  # both time out; the first transmission_ended handler clears the map
	assert_eq(monitor.get_radios().size(), 0)


func test_pdulog_with_non_finite_time() -> void:
	DirAccess.make_dir_recursive_absolute(DIR)
	var recording := DisRecording.new()
	recording.add_packet(_entity_state(1).encode(), 1793534400.0, "10.0.0.7", 3000, "239.1.2.3", 3000)
	recording.add_packet(_entity_state(2).encode(), 1793534401.0, "10.0.0.7", 3000, "239.1.2.3", 3000)
	var path := DIR.path_join("nan.pdulog")
	assert_eq(recording.save(path), OK)
	var lines := FileAccess.get_file_as_string(path).split("\n")
	for i in lines.size():
		if not lines[i].begins_with("#") and not lines[i].strip_edges().is_empty():
			lines[i] = "nan" + lines[i].substr(lines[i].find(" "))
			break
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_string("\n".join(lines))
	file.close()
	var loaded := DisRecording.new()
	loaded.load(path)
	assert_eq(loaded.get_packet_count(), 0, "a NaN time is a malformed line: %s" % loaded.get_last_error())


func test_body_axis_acceleration() -> void:
	var manager := _manager()
	var pdu := _entity_state(1)
	pdu.dr_algorithm = 9  # FVB: fixed orientation, velocity and acceleration, body axes
	pdu.orientation = Vector3(0.7, 0.2, -0.3)  # psi, theta, phi in radians
	pdu.dr_linear_acceleration = Vector3(10, 0, 0)  # body x: straight ahead
	var clock := manager.get_clock()
	manager.handle_entity_state(pdu, clock)
	manager.process_entities(clock + 2.0)
	var entity: DisRemoteEntity = manager.get_entities()[0]
	var forward := -entity.global_basis.z.normalized()
	var moved := entity.global_position
	assert_almost_eq(moved.length(), 20.0, 0.05, "½ · 10 m/s² · (2 s)²")
	assert_almost_eq(moved.normalized().dot(forward), 1.0, 1e-4, "along the entity's own forward axis")


func test_entity_limit() -> void:
	var manager := _manager()
	manager.max_entities = 3
	var limits := []
	manager.entity_limit_reached.connect(func(id): limits.append(id))
	for i in 5:
		manager.handle_entity_state(_entity_state(i + 1))
	assert_eq(manager.get_entity_count(), 3, "only max_entities remote entities are created")
	assert_eq(manager.get_dropped_count(), 2)
	assert_eq(limits, [Vector3i(7, 1, 4)], "the signal fires once when the limit is reached")
	assert_push_warning_count(1, "and one warning")
	manager.handle_entity_state(_entity_state(1))
	assert_eq(manager.get_dropped_count(), 2, "known entities are still updated")
	manager.remove_entity(Vector3i(7, 1, 1))
	manager.handle_entity_state(_entity_state(6))
	assert_eq(manager.get_entity_count(), 3, "room again after a removal")
	manager.handle_entity_state(_entity_state(7))
	assert_eq(limits.size(), 2, "and the signal fires again at the next limit")
	manager.max_entities = 0
	manager.handle_entity_state(_entity_state(8))
	assert_eq(manager.get_entity_count(), 4, "0 means no limit")


func test_radio_limit() -> void:
	var monitor := DisRadioMonitor.new()
	monitor.max_radios = 2
	add_child_autofree(monitor)
	for i in 3:
		var pdu := DisTransmitterPdu.new()
		pdu.radio_reference_id = Vector3i(1, 1, i + 1)
		pdu.radio_number = 1
		monitor.handle_pdu(pdu)
	assert_eq(monitor.get_radios().size(), 2)
	assert_eq(monitor.get_dropped_count(), 1)
	assert_push_warning_count(1)


func test_bad_variable_parameter_fails_the_encode() -> void:
	# A parameter that is not 16 bytes: the encode fails instead of leaving it out (the length would not fit).
	for pdu: DisPduBase in [_entity_state(2), DisDetonationPdu.new(), DisEntityStateUpdatePdu.new()]:
		pdu.variable_parameters = [PackedByteArray([1, 2, 3])]
		assert_eq(pdu.encode(), PackedByteArray(), pdu.get_class())
		assert_engine_error("not 16 bytes")
		var good := PackedByteArray()
		good.resize(16)
		pdu.variable_parameters = [good]
		assert_eq(pdu.encode().size(), pdu.get_length(), pdu.get_class() + " with a good parameter")


func test_signal_data_length_must_fit_its_field() -> void:
	var pdu := DisSignalPdu.new()
	pdu.data_length = 70000
	assert_eq(pdu.encode(), PackedByteArray(), "more bits than the 16-bit field holds")
	assert_engine_error("16-bit field")
	pdu.data_length = 65535
	assert_eq(pdu.encode().size(), pdu.get_length())


func test_last_error_is_cleared_by_the_next_decode() -> void:
	assert_null(DisPdu.decode(PackedByteArray([7, 1])))
	assert_ne(DisPdu.get_last_error(), "")
	assert_null(DisPdu.decode(PackedByteArray([0, 1, 1, 1, 0, 0, 0, 0, 0, 12, 0, 0])), "bad version")
	assert_string_contains(DisPdu.get_last_error(), "version")
	assert_not_null(DisPdu.decode(_entity_state(3).encode()))
	assert_eq(DisPdu.get_last_error(), "")


func test_comment_hides_the_fields_it_does_not_have() -> void:
	var names := DisCommentPdu.new().get_property_list().filter(
			func(p: Dictionary) -> bool: return p.usage & PROPERTY_USAGE_EDITOR).map(
			func(p: Dictionary) -> String: return p.name)
	assert_does_not_have(names, "request_id")
	assert_does_not_have(names, "padding")
	var data_names := DisDataPdu.new().get_property_list().map(func(p: Dictionary) -> String: return p.name)
	assert_has(data_names, "request_id")
