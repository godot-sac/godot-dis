extends SceneTree
## Performance check for Phase 4: N remote entities updated at R Hz.
## <godot> --headless --path . -s res://tools/perf_entities.gd -- [entities] [rate_hz] [seconds]
## Defaults: 1000 entities, 5 Hz, 5 s.
##  1. API only: cost of handle_entity_state() and of one process_entities() (dead reckoning + node transforms).
##  2. Over UDP (loopback): a sender network sends N x R PDUs per second, a manager on another network shows them.

var _entities := 1000
var _rate := 5.0
var _seconds := 5.0

var _origin: DisGeoOrigin
var _manager: DisEntityManager
var _sender: DisNetwork
var _receiver: DisNetwork
var _pdus: Array[DisEntityStatePdu] = []
var _elapsed := 0.0
var _to_send := 0.0
var _next := 0
var _frames := 0
var _process_usec := 0
var _updates := 0
var _phase := 0


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		_entities = int(args[0])
	if args.size() > 1:
		_rate = float(args[1])
	if args.size() > 2:
		_seconds = float(args[2])

	_origin = DisGeoOrigin.new()
	_origin.set_origin_geodetic(39.9334, 32.8597, 938.0)
	root.add_child(_origin)
	var rng := RandomNumberGenerator.new()
	rng.seed = 1
	for i in _entities:
		var pdu := DisEntityStatePdu.new()
		pdu.entity_id = Vector3i(7, 8, i + 1)
		pdu.location_ecef = _origin.local_to_ecef(Vector3(rng.randf_range(-20000, 20000), rng.randf_range(0, 3000),
				rng.randf_range(-20000, 20000)))
		pdu.linear_velocity = _origin.local_vector_to_ecef(Vector3(rng.randf_range(-50, 50), 0, rng.randf_range(-50, 50)))
		pdu.orientation = _origin.basis_to_dis_euler(Basis(Vector3.UP, rng.randf_range(-PI, PI)))
		pdu.dr_angular_velocity = Vector3(0, 0, rng.randf_range(-0.2, 0.2))
		pdu.dr_algorithm = 4
		_pdus.append(pdu)
	# The benchmarks start in _process: during _initialize the root is not in the tree yet.


func _api_benchmark() -> void:
	var manager := DisEntityManager.new()
	manager.auto_update = false
	root.add_child(manager)
	var start := Time.get_ticks_usec()
	for pdu in _pdus:
		manager.handle_entity_state(pdu, 0.0)
	var create_ms := (Time.get_ticks_usec() - start) / 1000.0
	start = Time.get_ticks_usec()
	for pdu in _pdus:
		manager.handle_entity_state(pdu, 0.1)
	var update_us := float(Time.get_ticks_usec() - start) / _pdus.size()
	start = Time.get_ticks_usec()
	for i in 100:
		manager.process_entities(0.2 + i / 60.0)
	var process_ms := (Time.get_ticks_usec() - start) / 100000.0
	print("API: %d entities created in %.1f ms; update %.1f us/PDU; process_entities %.2f ms/frame"
			% [_entities, create_ms, update_us, process_ms])
	print("     -> %d x %.0f Hz = %.0f PDU/s costs %.2f ms per second of PDU handling"
			% [_entities, _rate, _entities * _rate, _entities * _rate * update_us / 1000.0])
	manager.free()


func _start_udp() -> void:
	_sender = DisNetwork.new()
	_sender.port = 3190
	_sender.mode = DisNetwork.MODE_UNICAST
	_sender.destination_address = "127.0.0.1"
	_sender.destination_port = 3191
	root.add_child(_sender)
	_receiver = DisNetwork.new()
	_receiver.port = 3191
	_receiver.site_id = 99
	_receiver.max_packets_per_frame = 100000
	root.add_child(_receiver)
	_manager = DisEntityManager.new()
	_manager.auto_update = false  # timed below
	_manager.entity_updated.connect(func(_e): _updates += 1)
	_manager.entity_appeared.connect(func(_e): _updates += 1)
	root.add_child(_manager)
	_manager.network_path = _manager.get_path_to(_receiver)


func _process(delta: float) -> bool:
	if _phase == 0:
		_phase = 1
		_api_benchmark()
		_start_udp()
		return false
	_elapsed += delta
	_to_send += _entities * _rate * delta
	while _to_send >= 1.0:
		_sender.send_pdu(_pdus[_next])
		_next = (_next + 1) % _pdus.size()
		_to_send -= 1.0
	var start := Time.get_ticks_usec()
	_receiver.poll()  # receive here so the time is measured (DisNetwork also polls on its own)
	_manager.process_entities(Time.get_ticks_usec() / 1.0e6)
	_process_usec += Time.get_ticks_usec() - start
	_frames += 1
	if _elapsed < _seconds:
		return false
	var stats := _receiver.get_statistics()
	print("UDP: %.1f s, %d frames (%.0f fps), sent %d, received %d, rejected %d, entities %d"
			% [_elapsed, _frames, _frames / _elapsed, _sender.get_statistics().sent, stats.received, stats.rejected,
			_manager.get_entity_count()])
	print("     receive + decode + manager: %.2f ms/frame average, %d entity updates"
			% [_process_usec / 1000.0 / _frames, _updates])
	return true
