extends GutTest
## DisNetwork over real UDP sockets on this machine, including Open-DIS Python as the other side.
## Ports 31xx are used so a DIS application on 3000 doesn't interfere.

## venv Python: Windows layout first, then Linux / macOS.
const PYTHON_PATHS: PackedStringArray = ["res://tools/.venv/Scripts/python.exe", "res://tools/.venv/bin/python"]

var _received: Array = []
var _rejected: Array = []


func before_each() -> void:
	_received.clear()
	_rejected.clear()


func _network(port: int, mode: int = DisNetwork.MODE_UNICAST, destination := "127.0.0.1",
		destination_port := 0) -> DisNetwork:
	var network := DisNetwork.new()
	network.port = port
	network.mode = mode
	network.destination_address = destination
	network.destination_port = destination_port
	network.entity_state_received.connect(func(pdu): _received.append(pdu))
	network.packet_rejected.connect(func(reason, ip): _rejected.append(reason))
	add_child_autofree(network)
	return network


func _entity(entity_number: int, site := 1, application := 1) -> DisEntityStatePdu:
	var pdu := DisEntityStatePdu.new()
	pdu.entity_id = Vector3i(site, application, entity_number)
	pdu.marking = "GD%d" % entity_number
	pdu.location_ecef = PackedFloat64Array([4118639.5, 2654282.25, 4069473.125 + entity_number])
	return pdu


## Waits until `condition` returns true or `seconds` pass. Returns the final condition value.
func _wait_for(condition: Callable, seconds: float) -> bool:
	var deadline := Time.get_ticks_msec() + int(seconds * 1000)
	while not condition.call() and Time.get_ticks_msec() < deadline:
		await wait_process_frames(1)
	return condition.call()


func _python() -> String:
	for candidate in PYTHON_PATHS:
		var path := ProjectSettings.globalize_path(candidate)
		if FileAccess.file_exists(path):
			return path
	return ""


func test_unicast_loopback_between_two_networks() -> void:
	var sender := _network(3101, DisNetwork.MODE_UNICAST, "127.0.0.1", 3102)
	var receiver := _network(3102)
	receiver.site_id = 99  # different simulation, so the packet isn't "own"
	assert_true(sender.is_running() and receiver.is_running(), "sockets bound")
	assert_eq(sender.send_pdu(_entity(5)), OK)
	assert_true(await _wait_for(func(): return _received.size() >= 1, 2.0), "packet arrived")
	var pdu: DisEntityStatePdu = _received[0]
	assert_eq(pdu.entity_id, Vector3i(1, 1, 5))
	assert_eq(pdu.marking, "GD5")
	assert_eq(pdu.location_ecef[2], 4069473.125 + 5)
	assert_eq(sender.get_statistics().sent, 1)
	assert_eq(receiver.get_statistics().received, 1)


func test_exercise_filter() -> void:
	var sender := _network(3103, DisNetwork.MODE_UNICAST, "127.0.0.1", 3104)
	var receiver := _network(3104)
	receiver.site_id = 99
	sender.exercise_id = 2  # send_pdu stamps the network's exercise ID
	sender.send_pdu(_entity(1))
	await wait_seconds(0.3)
	assert_eq(_received.size(), 0, "other exercise is ignored")
	assert_eq(receiver.get_statistics().ignored, 1)
	receiver.filter_exercise = false
	sender.send_pdu(_entity(2))
	assert_true(await _wait_for(func(): return _received.size() == 1, 2.0), "accepted with filter off")


func test_own_packets_are_ignored() -> void:
	# Same site/application on both ends: the receiver treats the packet as its own echo.
	var sender := _network(3105, DisNetwork.MODE_UNICAST, "127.0.0.1", 3106)
	var receiver := _network(3106)
	sender.send_pdu(_entity(1, 1, 1))
	await wait_seconds(0.3)
	assert_eq(_received.size(), 0)
	assert_eq(receiver.get_statistics().ignored, 1)
	receiver.ignore_own_packets = false
	sender.send_pdu(_entity(2, 1, 1))
	assert_true(await _wait_for(func(): return _received.size() == 1, 2.0))


func test_malformed_packets_are_rejected() -> void:
	var sender := _network(3107, DisNetwork.MODE_UNICAST, "127.0.0.1", 3108)
	_network(3108)
	sender.send_bytes(PackedByteArray([7, 1, 1]))
	var truncated := _entity(1, 5, 5).encode().slice(0, 100)
	sender.send_bytes(truncated)
	assert_true(await _wait_for(func(): return _rejected.size() == 2, 2.0), str(_rejected))
	assert_eq(_received.size(), 0)


## Broadcast addresses to try: the limited broadcast first, then x.y.z.255 for each private IPv4
## address (a /24 guess). On machines with VPN / WSL adapters Windows may send 255.255.255.255 out of
## another adapter, while the subnet-directed broadcast reaches the local network and loops back.
func _broadcast_candidates() -> PackedStringArray:
	var candidates := PackedStringArray(["255.255.255.255"])
	for address in IP.get_local_addresses():
		if address.begins_with("192.168.") or address.begins_with("10."):
			var parts := address.split(".")
			candidates.append("%s.%s.%s.255" % [parts[0], parts[1], parts[2]])
	return candidates


func test_broadcast() -> void:
	var receiver := _network(3110)
	receiver.site_id = 99
	var tried := PackedStringArray()
	for i in _broadcast_candidates().size():
		var address := _broadcast_candidates()[i]
		var sender := _network(3130 + i, DisNetwork.MODE_BROADCAST, address, 3110)
		sender.send_pdu(_entity(1))
		tried.append(address)
		if await _wait_for(func(): return _received.size() >= 1, 1.0):
			assert_eq((_received[0] as DisEntityStatePdu).marking, "GD1", "via " + address)
			return
	pending("no broadcast looped back on this machine (tried %s)" % ", ".join(tried))


func test_multicast() -> void:
	var receiver := DisNetwork.new()
	receiver.mode = DisNetwork.MODE_MULTICAST
	receiver.port = 3112
	receiver.site_id = 99
	receiver.entity_state_received.connect(func(pdu): _received.append(pdu))
	add_child_autofree(receiver)
	if not receiver.is_running():
		pending("could not join a multicast group on any interface")
		return
	var sender := _network(3111, DisNetwork.MODE_UNICAST, receiver.multicast_group, 3112)
	sender.send_pdu(_entity(3))
	if not await _wait_for(func(): return _received.size() >= 1, 2.0):
		pending("multicast did not loop back (joined: %s)" % receiver.get_statistics().multicast_interfaces)
		return
	assert_eq((_received[0] as DisEntityStatePdu).entity_id.z, 3)


func test_two_applications_share_a_port() -> void:
	# Like two DIS applications on one computer: both listen on 3114 (multicast) and both get a third one's PDU.
	var got := {"a": 0, "b": 0}
	var networks: Array[DisNetwork] = []
	for site in [11, 12]:
		var network := DisNetwork.new()
		network.mode = DisNetwork.MODE_MULTICAST
		network.port = 3114
		network.site_id = site
		add_child_autofree(network)
		networks.append(network)
	assert_true(networks[0].is_running() and networks[1].is_running(), "both bound port 3114")
	if not (networks[0].is_running() and networks[1].is_running()):
		return
	networks[0].entity_state_received.connect(func(_pdu): got.a += 1)
	networks[1].entity_state_received.connect(func(_pdu): got.b += 1)
	var sender := _network(3115, DisNetwork.MODE_UNICAST, networks[0].multicast_group, 3114)
	sender.site_id = 13
	sender.send_pdu(_entity(1, 13, 1))
	assert_true(await _wait_for(func(): return got.a >= 1 and got.b >= 1, 2.0), "both received: %s" % got)
	# And they see each other: site 11 sends, site 12 receives (site 11 ignores its own echo).
	networks[0].send_pdu(_entity(2, 11, 1))
	assert_true(await _wait_for(func(): return got.b >= 2, 2.0), "site 12 got site 11's PDU: %s" % got)
	assert_eq(got.a, 1, "own PDU ignored")


func test_exclusive_port_when_not_shared() -> void:
	var first := DisNetwork.new()
	first.port = 3116
	first.share_port = false
	add_child_autofree(first)
	var second := DisNetwork.new()
	second.port = 3116
	second.share_port = false
	second.autostart = false
	add_child_autofree(second)
	assert_true(first.is_running())
	assert_ne(second.start(), OK, "port already taken")
	assert_false(second.is_running())
	assert_push_error_count(1, "the bind failure is reported")


func test_receives_from_open_dis_python() -> void:
	var python := _python()
	if python.is_empty():
		pending("Python oracle venv not found (tools/.venv)")
		return
	_network(3120)
	var pid := OS.create_process(python, [ProjectSettings.globalize_path("res://tools/oracle_sender.py"),
			"--port", "3120", "--count", "5", "--repeat", "3", "--site", "7", "--application", "8"])
	assert_gt(pid, 0, "oracle sender started")
	var unique := {}
	await _wait_for(func():
		for pdu: DisEntityStatePdu in _received:
			unique[pdu.entity_id.z] = pdu
		return unique.size() >= 5, 10.0)
	assert_eq(unique.size(), 5, "all five entities from Python arrived")
	for n in unique:
		var pdu: DisEntityStatePdu = unique[n]
		assert_eq(pdu.entity_id, Vector3i(7, 8, n))
		assert_eq(pdu.marking, "PY%d" % n)
		assert_eq(pdu.location_ecef, PackedFloat64Array([n * 1000.0, 2 * n * 1000.0, 3 * n * 1000.0]))
		assert_eq(pdu.force_id, 1 + n % 3)


func test_sends_to_open_dis_python() -> void:
	var python := _python()
	if python.is_empty():
		pending("Python oracle venv not found (tools/.venv)")
		return
	var out := OS.get_user_data_dir().path_join("oracle_received.jsonl")
	for path in [out, out + ".ready", out + ".done"]:
		if FileAccess.file_exists(path):
			DirAccess.remove_absolute(path)
	var pid := OS.create_process(python, [ProjectSettings.globalize_path("res://tools/oracle_receiver.py"),
			"--port", "3121", "--out", out, "--expect", "3", "--timeout", "10"])
	assert_gt(pid, 0, "oracle receiver started")
	assert_true(await _wait_for(func(): return FileAccess.file_exists(out + ".ready"), 10.0), "receiver ready")
	var sender := _network(3122, DisNetwork.MODE_UNICAST, "127.0.0.1", 3121)
	sender.exercise_id = 4
	for n in [1, 2, 3]:
		sender.send_pdu(_entity(n))
	assert_true(await _wait_for(func(): return FileAccess.file_exists(out + ".done"), 12.0), "receiver finished")
	var lines := Array(FileAccess.get_file_as_string(out).split("\n", false))
	assert_eq(lines.size(), 3, "Python decoded three PDUs")
	for i in lines.size():
		var row: Dictionary = JSON.parse_string(lines[i])
		assert_eq(row.get("type"), "EntityStatePdu", str(row))
		assert_eq(row.get("exercise_id"), 4.0)
		assert_eq(row.get("entity_id"), [1.0, 1.0, float(i + 1)])
		assert_eq(row.get("marking"), "GD%d" % (i + 1))
		assert_eq(row.get("length_field"), 144.0)
		assert_almost_eq(row.location_ecef[2], 4069473.125 + i + 1, 1e-6)
