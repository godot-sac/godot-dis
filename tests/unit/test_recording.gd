extends GutTest
## DisRecording, DisRecorder and DisPlayer (loopback UDP on ports 316x, files in user://test_recordings/).

const DIR := "user://test_recordings/"

var _origin: DisGeoOrigin


func before_all() -> void:
	DirAccess.make_dir_recursive_absolute(DIR)


func before_each() -> void:
	_origin = DisGeoOrigin.new()
	_origin.set_origin_geodetic(39.9334, 32.8597, 938.0)
	add_child_autofree(_origin)


func _network(port: int, destination_port := 0, site := 1, autostart := true) -> DisNetwork:
	var network := DisNetwork.new()
	network.mode = DisNetwork.MODE_UNICAST
	network.port = port
	network.destination_address = "127.0.0.1"
	network.destination_port = destination_port
	network.site_id = site
	network.autostart = autostart
	add_child_autofree(network)
	return network


## Entity State PDU of entity `n` at local position `position` moving with `velocity` (m/s, Godot axes).
func _entity_state(n: int, position: Vector3, velocity := Vector3.ZERO, site := 7) -> DisEntityStatePdu:
	var pdu := DisEntityStatePdu.new()
	pdu.entity_id = Vector3i(site, 1, n)
	pdu.entity_type = PackedInt32Array([1, 1, 225, 1, 1, 0, 0])
	pdu.marking = "E%d" % n
	pdu.location_ecef = _origin.local_to_ecef(position)
	pdu.linear_velocity = _origin.local_vector_to_ecef(velocity)
	pdu.dr_algorithm = 2
	return pdu


func _wait_for(condition: Callable, seconds: float) -> bool:
	var deadline := Time.get_ticks_msec() + int(seconds * 1000)
	while not condition.call() and Time.get_ticks_msec() < deadline:
		await wait_process_frames(1)
	return condition.call()


func _sample_recording() -> DisRecording:
	var recording := DisRecording.new()
	var t0 := 1793534400.0
	for i in 10:
		recording.add_packet(_entity_state(1, Vector3(i * 10.0, 0, 0), Vector3(10, 0, 0)).encode(), t0 + i,
				"10.0.0.7", 3000, "239.1.2.3", 3000)
	var fire := DisFirePdu.new()
	fire.firing_entity_id = Vector3i(7, 1, 1)
	recording.add_packet(fire.encode(), t0 + 4.5, "10.0.0.7", 3000, "239.1.2.3", 3000)
	return recording


func test_recording_round_trip_in_both_formats() -> void:
	var recording := _sample_recording()
	assert_eq(recording.get_packet_count(), 11)
	assert_almost_eq(recording.get_duration(), 9.0, 1e-6)
	assert_eq(recording.get_packet(5).bytes[2], DisPdu.PDU_TYPE_FIRE, "add_packet keeps time order")
	for file in ["round_trip.pcapng", "round_trip.pdulog"]:
		assert_eq(recording.save(DIR + file), OK, file)
		var loaded := DisRecording.new()
		assert_eq(loaded.load(DIR + file), OK, file)
		assert_eq(loaded.get_packet_count(), 11, file)
		for i in 11:
			assert_eq(loaded.get_bytes(i), recording.get_bytes(i), "%s #%d bytes" % [file, i])
			assert_almost_eq(loaded.get_time(i), recording.get_time(i), 2e-6, "%s #%d time" % [file, i])
		var packet := loaded.get_packet(3)
		assert_eq([packet.source_ip, packet.source_port, packet.destination_ip], ["10.0.0.7", 3000, "239.1.2.3"])
		assert_almost_eq(loaded.get_start_time(), 1793534400.0, 1e-5)
	var summary := recording.get_summary()
	assert_eq(summary.packets, 11)
	assert_eq(summary.pdu_types, {1: 10, 2: 1})
	assert_eq(summary.entities, 1)
	assert_eq(summary.sites, ["7:1"])
	assert_eq(recording.find_index(4.2), 5)
	assert_eq(recording.find_index(100.0), 11)


func test_load_errors() -> void:
	var recording := DisRecording.new()
	assert_ne(recording.load(DIR + "does_not_exist.pcapng"), OK)
	var file := FileAccess.open(DIR + "garbage.pcapng", FileAccess.WRITE)
	file.store_buffer(PackedByteArray([0xFF, 0x00, 0x12, 0x34, 0x56]))
	file.close()
	assert_ne(recording.load(DIR + "garbage.pcapng"), OK)
	assert_ne(recording.get_last_error(), "")


func test_recorder_writes_received_and_sent_packets() -> void:
	for file in ["recorded.pcapng", "recorded.pdulog"]:
		var sender := _network(3160, 3161, 1)
		var receiver := _network(3161, 3160, 2)
		var recorder := DisRecorder.new()
		add_child_autofree(recorder)
		recorder.network_path = recorder.get_path_to(receiver)
		assert_eq(recorder.start(DIR + file), OK)
		sender.send_pdu(_entity_state(1, Vector3.ZERO, Vector3.ZERO, 1))
		var fire := DisFirePdu.new()
		fire.firing_entity_id = Vector3i(1, 1, 1)
		sender.send_pdu(fire)
		var garbage := PackedByteArray([1, 2, 3])
		sender.send_bytes(garbage)  # recorded too: the recorder sees datagrams before any filter
		await _wait_for(func(): return receiver.get_statistics().received >= 3, 2.0)
		receiver.send_pdu(_entity_state(9, Vector3.ZERO, Vector3.ZERO, 2))  # sent by the recording side
		assert_eq(recorder.get_packet_count(), 4, file + ": 3 received + 1 sent")
		recorder.stop()
		var loaded := DisRecording.new()
		loaded.load(DIR + file)
		assert_eq(loaded.get_packet_count(), 3, file + ": the 3-byte datagram is not a PDU, readers skip it")
		assert_eq(loaded.get_read_statistics().not_dis, 1, file)
		var types := []
		for i in loaded.get_packet_count():
			types.append(loaded.get_bytes(i)[2])
		assert_eq(types, [1, 2, 1], file)
		assert_eq(loaded.get_packet(2).source_port, 3161, file + ": our own PDU, from our port")
		sender.queue_free()
		receiver.queue_free()
		recorder.queue_free()
		await wait_process_frames(2)


func test_own_packets_looping_back_are_recorded_once() -> void:
	var network := DisNetwork.new()
	network.mode = DisNetwork.MODE_UNICAST
	network.port = 3162
	network.destination_address = "127.0.0.1"  # to ourselves: like a multicast loop-back
	add_child_autofree(network)
	var recorder := DisRecorder.new()
	add_child_autofree(recorder)
	recorder.network_path = recorder.get_path_to(network)
	recorder.start(DIR + "own.pcapng")
	network.send_pdu(_entity_state(1, Vector3.ZERO, Vector3.ZERO, 1))
	await _wait_for(func(): return network.get_statistics().received >= 1, 2.0)
	assert_eq(network.get_statistics().ignored, 1, "the network ignores it")
	assert_eq(recorder.get_packet_count(), 1, "recorded as sent, not again as received")
	recorder.record_sent = false
	network.send_pdu(_entity_state(2, Vector3.ZERO, Vector3.ZERO, 1))
	await _wait_for(func(): return network.get_statistics().received >= 2, 2.0)
	assert_eq(recorder.get_packet_count(), 2, "without record_sent the looped-back copy is recorded")
	recorder.stop()


func test_player_local_output_and_seek() -> void:
	var network := _network(3163, 0, 1, false)  # not started: local playback needs no socket
	var manager := DisEntityManager.new()
	manager.smoothing_time = 0.0
	add_child_autofree(manager)
	manager.network_path = manager.get_path_to(network)
	manager.origin_path = manager.get_path_to(_origin)
	var player := DisPlayer.new()
	add_child_autofree(player)
	player.network_path = player.get_path_to(network)
	player.entity_manager_path = player.get_path_to(manager)
	player.recording = _sample_recording()
	var fires := []
	network.fire_received.connect(func(pdu): fires.append(pdu))

	player.advance(2.5)
	assert_eq(manager.get_entity_count(), 1, "the recorded entity appears")
	assert_eq(network.get_statistics().injected, 3)
	player.advance(2.5)
	assert_eq(fires.size(), 1, "the Fire PDU at 4.5 s was played")

	player.seek(7.5)
	assert_eq(manager.get_entity_count(), 1, "seek rebuilds the picture")
	var entity: DisRemoteEntity = manager.get_entities()[0]
	manager.process_entities(manager.get_clock())
	assert_almost_eq(entity.global_position.x, 75.0, 0.5, "last report at 7 s (70 m) dead reckoned 0.5 s at 10 m/s")
	assert_eq(fires.size(), 1, "seeking does not replay events")

	player.speed = 4.0
	player.play()
	assert_eq(manager.time_scale, 4.0, "the manager's clock follows the playback speed")
	player.pause()
	assert_eq(manager.time_scale, 0.0, "and stands still while paused")


func test_player_finishes_and_loops() -> void:
	var network := _network(3164, 0, 1, false)
	var player := DisPlayer.new()
	add_child_autofree(player)
	player.network_path = player.get_path_to(network)
	player.recording = _sample_recording()
	watch_signals(player)
	player.play()
	player.advance(20.0)
	assert_signal_emitted(player, "finished")
	assert_false(player.is_playing())
	assert_eq(network.get_statistics().injected, 11)
	player.loop = true
	player.play()
	player.advance(9.5)
	assert_true(player.is_playing(), "loops instead of finishing")
	assert_signal_emitted(player, "seeked")


func test_player_network_output() -> void:
	var sender := _network(3165, 3166, 1)
	var receiver := _network(3166, 0, 2)
	receiver.filter_exercise = false
	var received := []
	receiver.pdu_received.connect(func(pdu, ip, port): received.append(pdu))
	var player := DisPlayer.new()
	add_child_autofree(player)
	player.network_path = player.get_path_to(sender)
	player.output = DisPlayer.OUTPUT_NETWORK
	player.exercise_id_override = 9
	player.recording = _sample_recording()
	player.advance(100.0)
	assert_true(await _wait_for(func(): return received.size() == 11, 2.0), "all 11 sent over UDP")
	assert_eq(received[0].exercise_id, 9, "exercise_id_override")
	assert_eq(sender.get_statistics().injected, 0, "nothing played into the local scene")
