extends GutTest
## DisExerciseControl: a simulation manager and a managed application on loopback UDP (ports 313x).

const ALL := DisSimulationManagementPdu.ID_ALL

var _manager: DisExerciseControl
var _managed: DisExerciseControl
var _manager_network: DisNetwork
var _managed_network: DisNetwork
var _events: Array = []


func _network(port: int, destination_port: int, site: int) -> DisNetwork:
	var network := DisNetwork.new()
	network.mode = DisNetwork.MODE_UNICAST
	network.port = port
	network.destination_address = "127.0.0.1"
	network.destination_port = destination_port
	network.site_id = site
	return network


## Two little scenes side by side: [manager network + control] and [managed network + control + origin].
func _setup(base_port: int, configure := Callable()) -> void:
	_events.clear()
	var manager_root := Node.new()
	_manager_network = _network(base_port, base_port + 1, 1)
	manager_root.add_child(_manager_network)
	_manager = DisExerciseControl.new()
	_manager.network_path = NodePath("../" + str(_manager_network.name))
	_manager.retry_interval = 0.2
	_manager.max_retries = 2
	manager_root.add_child(_manager)

	var managed_root := Node.new()
	_managed_network = _network(base_port + 1, base_port, 2)
	managed_root.add_child(_managed_network)
	_managed = DisExerciseControl.new()
	_managed.network_path = NodePath("../" + str(_managed_network.name))
	if configure.is_valid():
		configure.call(_managed)
	managed_root.add_child(_managed)

	_manager.acknowledged.connect(func(id, response, pdu): _events.append(["ack", id, response, pdu.acknowledge_flag]))
	_manager.request_failed.connect(func(id): _events.append(["failed", id]))
	_managed.started.connect(func(pdu): _events.append(["started", pdu]))
	_managed.stopped.connect(func(pdu): _events.append(["stopped", pdu]))
	_managed.remove_entity_requested.connect(func(id, pdu): _events.append(["remove", id]))
	add_child_autofree(manager_root)
	add_child_autofree(managed_root)


func _count(kind: String) -> int:
	return _events.filter(func(e): return e[0] == kind).size()


func _first(kind: String) -> Array:
	for e in _events:
		if e[0] == kind:
			return e
	return []


func _wait_for(condition: Callable, seconds: float) -> bool:
	var deadline := Time.get_ticks_msec() + int(seconds * 1000)
	while not condition.call() and Time.get_ticks_msec() < deadline:
		await wait_process_frames(1)
	return condition.call()


func test_stop_and_resume_with_acknowledge() -> void:
	_setup(3130)
	assert_true(_managed.is_running(), "applications start running by default")
	var stop_id := _manager.send_stop_freeze(Vector3i(ALL, ALL, ALL), DisStopFreezePdu.REASON_RECESS,
			DisStopFreezePdu.FROZEN_TRANSMIT_UPDATES)
	assert_gt(stop_id, 0)
	assert_true(await _wait_for(func(): return _count("ack") == 1, 2.0), "stop acknowledged")
	assert_eq(_managed.get_state(), DisExerciseControl.STATE_STOPPED)
	assert_eq(_managed.get_frozen_behavior(), DisStopFreezePdu.FROZEN_TRANSMIT_UPDATES)
	var stopped: DisStopFreezePdu = _first("stopped")[1]
	assert_eq(stopped.reason, DisStopFreezePdu.REASON_RECESS)
	assert_eq(stopped.originating_id, Vector3i(1, 1, 0), "manager's site and application")
	assert_eq(_first("ack"), ["ack", stop_id, DisAcknowledgePdu.RESPONSE_ABLE_TO_COMPLY,
			DisAcknowledgePdu.ACKNOWLEDGE_STOP_FREEZE])

	var start_id := _manager.send_start_resume(Vector3i(2, 1, 0), 0.0, 120.0)
	assert_true(await _wait_for(func(): return _count("ack") == 2, 2.0), "start acknowledged")
	assert_true(_managed.is_running())
	assert_almost_eq(_managed.get_simulation_time(), 120.0, 1.0, "simulation clock set by Start/Resume")
	assert_eq(_events.back()[1], start_id)
	await wait_seconds(0.3)
	assert_eq(_count("ack"), 2, "requests to one application are not resent once answered")
	var pending := _manager.get_pending_requests()
	assert_eq(pending.size(), 1, "the request to everyone waits for more answers")
	assert_eq(pending[0].request_id, stop_id)
	assert_eq(pending[0].answers, 1)
	await wait_seconds(0.5)
	assert_eq(_manager.get_pending_requests().size(), 0, "then it is dropped")
	assert_eq(_count("failed"), 0, "answered requests don't fail")


func test_requests_to_other_applications_are_ignored() -> void:
	_setup(3132)
	_manager.send_stop_freeze(Vector3i(5, 5, 0))
	await wait_seconds(0.3)
	assert_true(_managed.is_running(), "site 5 is not us")
	assert_eq(_count("ack"), 0)


func test_unanswered_request_is_resent_then_fails() -> void:
	_setup(3134, func(control): control.auto_acknowledge = false)
	var stops := []
	_managed_network.stop_freeze_received.connect(func(pdu): stops.append(pdu))
	var id := _manager.send_stop_freeze(Vector3i(2, 1, 0))
	assert_true(await _wait_for(func(): return _count("failed") == 1, 3.0), "request_failed after the retries")
	assert_eq(_first("failed"), ["failed", id])
	assert_eq(stops.size(), 3, "sent once plus max_retries (2) times")
	assert_eq(_count("stopped"), 1, "a resent request is carried out only once")


func test_scheduled_start() -> void:
	_setup(3136, func(control): control.start_stopped = true)
	assert_false(_managed.is_running(), "start_stopped")
	var at := Time.get_unix_time_from_system() + 0.6
	_manager.send_start_resume(Vector3i(ALL, ALL, ALL), at)
	assert_true(await _wait_for(func(): return _count("ack") == 1, 2.0), "acknowledged at once")
	assert_false(_managed.is_running(), "but not started yet")
	assert_almost_eq(_managed.get_scheduled_time(), at, 0.01)
	assert_true(await _wait_for(func(): return _managed.is_running(), 2.0), "started at the requested time")
	assert_gt(Time.get_unix_time_from_system(), at - 0.05)


func test_remove_entity() -> void:
	_setup(3138)
	var entity := DisEntity.new()
	entity.entity_id = Vector3i(0, 0, 5)  # site / application from the managed network: 2:1:5
	entity.publishing = false
	entity.network_path = NodePath("../" + str(_managed_network.name))
	_managed.get_parent().add_child(entity)
	await wait_process_frames(1)
	assert_eq(entity.get_effective_entity_id(), Vector3i(2, 1, 5))

	_manager.send_remove_entity(Vector3i(2, 1, 99))
	assert_true(await _wait_for(func(): return _count("ack") == 1, 2.0))
	assert_eq(_first("ack")[2], DisAcknowledgePdu.RESPONSE_UNABLE_TO_COMPLY, "no such entity")

	_manager.send_remove_entity(Vector3i(2, 1, 5))
	assert_true(await _wait_for(func(): return _count("ack") == 2, 2.0))
	assert_eq(_events.back()[2], DisAcknowledgePdu.RESPONSE_ABLE_TO_COMPLY)
	assert_eq(_events.back()[3], DisAcknowledgePdu.ACKNOWLEDGE_REMOVE_ENTITY)
	assert_eq(_first("remove"), ["remove", Vector3i(2, 1, 5)])
	await wait_process_frames(2)
	assert_false(is_instance_valid(entity), "remove_entities frees the DisEntity")


func test_manual_transitions_and_handle_pdu() -> void:
	_setup(3140)
	var changes := []
	_managed.state_changed.connect(func(state): changes.append(state))
	_managed.stop(DisStopFreezePdu.FROZEN_RUN_SIMULATION_CLOCK)
	assert_false(_managed.is_running())
	var before := _managed.get_simulation_time()
	await wait_seconds(0.2)
	assert_gt(_managed.get_simulation_time(), before, "the clock runs while frozen with FROZEN_RUN_SIMULATION_CLOCK")
	var start := DisStartResumePdu.new()
	start.originating_id = Vector3i(9, 9, 0)
	start.receiving_id = Vector3i(ALL, ALL, ALL)
	start.request_id = 77
	_managed.handle_pdu(start)
	assert_true(_managed.is_running(), "handle_pdu applies a PDU without the network")
	assert_eq(changes, [DisExerciseControl.STATE_STOPPED, DisExerciseControl.STATE_RUNNING])


func test_create_entity_request() -> void:
	_setup(3142)
	_manager.send_create_entity(Vector3i(2, 1, 0))
	assert_true(await _wait_for(func(): return _count("ack") == 1, 2.0))
	assert_eq(_first("ack")[2], DisAcknowledgePdu.RESPONSE_UNABLE_TO_COMPLY, "nobody handles create_entity_requested")
	assert_eq(_first("ack")[3], DisAcknowledgePdu.ACKNOWLEDGE_CREATE_ENTITY)
	var requests := []
	_managed.create_entity_requested.connect(func(pdu): requests.append(pdu))
	_manager.send_create_entity(Vector3i(2, 1, 0))
	assert_true(await _wait_for(func(): return _count("ack") == 2, 2.0))
	assert_eq(_events.back()[2], DisAcknowledgePdu.RESPONSE_ABLE_TO_COMPLY, "the game handles it")
	assert_eq(requests.size(), 1)
	assert_eq(requests[0].originating_id, Vector3i(1, 1, 0))
