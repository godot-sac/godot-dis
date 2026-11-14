extends GutTest
## DisEntity (publishing), DisEntityManager (receiving, dead reckoning, smoothing, removal) and both together
## over UDP. Most tests drive the nodes with explicit times (auto_update = false) so they are deterministic.

const ANKARA := [39.9334, 32.8597, 938.0]

var _published: Array = []
var _events: Array = []


func before_each() -> void:
	_published.clear()
	_events.clear()


func _origin() -> DisGeoOrigin:
	var origin := DisGeoOrigin.new()
	origin.set_origin_geodetic(ANKARA[0], ANKARA[1], ANKARA[2])
	add_child_autofree(origin)
	return origin


func _entity(dr_algorithm := 2) -> DisEntity:
	var entity := DisEntity.new()
	entity.auto_update = false
	entity.dr_algorithm = dr_algorithm
	entity.published.connect(func(pdu): _published.append(pdu))
	add_child_autofree(entity)
	return entity


func _manager() -> DisEntityManager:
	var manager := DisEntityManager.new()
	manager.auto_update = false
	manager.entity_appeared.connect(func(e): _events.append(["appeared", e]))
	manager.entity_updated.connect(func(e): _events.append(["updated", e]))
	manager.entity_removed.connect(func(e, reason): _events.append(["removed", e, reason]))
	add_child_autofree(manager)
	return manager


## A PDU as `origin` sees a node at `position` with `basis`, moving with `velocity` (all Godot space).
func _pdu(origin: DisGeoOrigin, id: Vector3i, position: Vector3, basis := Basis.IDENTITY,
		velocity := Vector3.ZERO, algorithm := 2) -> DisEntityStatePdu:
	var pdu := DisEntityStatePdu.new()
	pdu.entity_id = id
	pdu.location_ecef = origin.local_to_ecef(position)
	pdu.orientation = origin.basis_to_dis_euler(basis)
	pdu.linear_velocity = origin.local_vector_to_ecef(velocity)
	pdu.dr_algorithm = algorithm
	pdu.marking = "T%d" % id.z
	return pdu


# --- DisEntity -------------------------------------------------------------------------------------------

func test_first_update_publishes_position_and_id() -> void:
	var origin := _origin()
	var entity := _entity()
	entity.position = Vector3(100, 20, -300)
	entity.marking = "ALPHA"
	entity.process_entity(0.0)
	assert_eq(_published.size(), 1)
	var pdu: DisEntityStatePdu = _published[0]
	assert_eq(pdu.marking, "ALPHA")
	var id := entity.get_effective_entity_id()
	assert_eq(pdu.entity_id, id)
	assert_eq([id.x, id.y], [1, 1], "site / application default to 1 without a network")
	assert_gt(id.z, 0, "entity number assigned")
	assert_almost_eq(origin.ecef_to_local(pdu.location_ecef), Vector3(100, 20, -300), Vector3.ONE * 1e-3)
	assert_eq(entity.get_published_count(), 1)


func test_stationary_entity_sends_only_heartbeats() -> void:
	_origin()
	var entity := _entity()
	entity.heartbeat_interval = 5.0
	for i in 61:  # 0 .. 6 s at 10 Hz
		entity.process_entity(i * 0.1)
	assert_eq(_published.size(), 2, "first update and the 5 s heartbeat")


func test_constant_velocity_is_predicted_by_receivers() -> void:
	_origin()
	var entity := _entity(2)  # FPW: constant velocity
	entity.velocity_source = DisEntity.VELOCITY_MANUAL
	entity.linear_velocity = Vector3(20, 0, 0)
	for i in 41:  # 4 s at 10 Hz, moving exactly as reported
		entity.position = Vector3(20 * i * 0.1, 0, 0)
		entity.process_entity(i * 0.1)
	assert_eq(_published.size(), 1, "dead reckoning matches, nothing to correct")
	entity.position += Vector3(0, 0, 2.0)  # off by 2 m > 1 m threshold
	entity.process_entity(4.1)
	assert_eq(_published.size(), 2, "correction sent")


func test_orientation_threshold() -> void:
	_origin()
	var entity := _entity()
	entity.process_entity(0.0)
	entity.rotation_degrees = Vector3(0, 2, 0)
	entity.process_entity(0.1)
	assert_eq(_published.size(), 1, "2 degrees < 3 degree threshold")
	entity.rotation_degrees = Vector3(0, 4, 0)
	entity.process_entity(0.2)
	assert_eq(_published.size(), 2, "4 degrees")


func test_property_change_sends_immediately() -> void:
	_origin()
	var entity := _entity()
	entity.process_entity(0.0)
	entity.appearance = 1 << 3
	entity.process_entity(0.1)
	assert_eq(_published.size(), 2)
	assert_eq((_published[1] as DisEntityStatePdu).appearance, 1 << 3)


func test_estimated_velocity_and_angular_velocity() -> void:
	var origin := _origin()
	var entity := _entity(4)
	for i in 121:  # 2 s at 60 Hz: 10 m/s east, turning left at 0.5 rad/s
		var t := i / 60.0
		entity.position = Vector3(10 * t, 0, 0)
		entity.rotation = Vector3(0, 0.5 * t, 0)
		entity.process_entity(t)
	assert_almost_eq(entity.linear_velocity, Vector3(10, 0, 0), Vector3.ONE * 0.05)
	assert_almost_eq(entity.linear_acceleration.length(), 0.0, 0.1)
	assert_almost_eq(entity.angular_velocity, Vector3(0, 0.5, 0), Vector3.ONE * 0.01)
	var pdu := entity.create_pdu()
	assert_almost_eq(origin.ecef_vector_to_local(pdu.linear_velocity), Vector3(10, 0, 0), Vector3.ONE * 0.05)
	# DIS body axes: +Z is down, so turning left is a negative rate about body Z.
	assert_almost_eq(pdu.dr_angular_velocity, Vector3(0, 0, -0.5), Vector3.ONE * 0.01)


func test_turning_entity_with_rvw_needs_few_updates() -> void:
	_origin()
	var entity := _entity(4)
	for i in 601:  # 10 s at 60 Hz: circle of radius 200 m at 20 m/s
		var t := i / 60.0
		var angle := t * 0.1
		entity.position = Vector3(200 * sin(angle), 0, -200 * cos(angle))
		entity.basis = Basis(Vector3.UP, -angle)
		entity.process_entity(t)
	# Without dead reckoning this would be 601 PDUs; a constant turn is predicted well by DRM 4.
	assert_lt(_published.size(), 15, "published %d" % _published.size())



## Circle of radius 200 m at 20 m/s for 30 s; returns the number of PDUs sent with `algorithm`.
func _published_in_turn(algorithm: int) -> int:
	_published.clear()
	var entity := _entity(algorithm)
	for i in 1801:
		var t := i / 60.0
		var angle := t * 0.1
		entity.position = Vector3(200 * sin(angle), 0, -200 * cos(angle))
		entity.basis = Basis(Vector3.UP, -angle)
		entity.process_entity(t)
	entity.queue_free()
	return _published.size()


func test_turning_entity_with_body_axis_drm_needs_only_heartbeats() -> void:
	_origin()
	# DRM 7 (RPB) turns its velocity with the entity, so a steady turn is predicted exactly: one PDU per 5 s
	# heartbeat, plus a few while the estimated velocity and rates settle. DRM 3 (RPW) flies straight between
	# updates and needs many more.
	var rpb := _published_in_turn(7)
	var rpw := _published_in_turn(3)
	assert_lte(rpb, 12, "RPB published %d" % rpb)
	# DRM 8 (RVB): the centripetal acceleration is in the rates, so the body acceleration sent is about zero.
	var rvb := _published_in_turn(8)
	assert_lte(rvb, 12, "RVB published %d" % rvb)
	assert_gt(rpw, 3 * rpb, "RPW published %d" % rpw)


# --- DisEntityManager ------------------------------------------------------------------------------------

func test_manager_creates_and_places_entities() -> void:
	var origin := _origin()
	var manager := _manager()
	var basis := Basis(Vector3.UP, 0.7)
	manager.handle_entity_state(_pdu(origin, Vector3i(5, 6, 7), Vector3(50, 10, -80), basis), 0.0)
	assert_eq(manager.get_entity_count(), 1)
	var remote: DisRemoteEntity = manager.get_entity(Vector3i(5, 6, 7))
	assert_not_null(remote)
	assert_eq(_events[0][0], "appeared")
	assert_eq(remote.marking, "T7")
	assert_almost_eq(remote.global_position, Vector3(50, 10, -80), Vector3.ONE * 1e-3)
	assert_almost_eq(remote.global_basis.z, basis.z, Vector3.ONE * 1e-5)
	var geodetic := remote.get_geodetic()
	assert_almost_eq(geodetic[0], ANKARA[0], 0.01)
	manager.handle_entity_state(_pdu(origin, Vector3i(5, 6, 7), Vector3(51, 10, -80)), 0.1)
	assert_eq(_events[1][0], "updated")
	assert_eq(manager.get_entity_count(), 1)


func test_manager_dead_reckons() -> void:
	var origin := _origin()
	var manager := _manager()
	manager.smoothing_time = 0.0
	manager.handle_entity_state(_pdu(origin, Vector3i(1, 2, 3), Vector3.ZERO, Basis.IDENTITY,
			Vector3(10, 0, -5)), 0.0)
	manager.process_entities(2.0)
	var remote: DisRemoteEntity = manager.get_entity(Vector3i(1, 2, 3))
	# 2 s later; the tangent plane drops by r^2 / 2R ~ 0.00001 m here.
	assert_almost_eq(remote.global_position, Vector3(20, 0, -10), Vector3.ONE * 1e-3)
	manager.dead_reckoning = false
	manager.handle_entity_state(_pdu(origin, Vector3i(1, 2, 3), Vector3.ZERO, Basis.IDENTITY,
			Vector3(10, 0, 0)), 3.0)
	manager.process_entities(5.0)
	assert_almost_eq(remote.global_position, Vector3.ZERO, Vector3.ONE * 1e-3, "dead reckoning off")



func test_manager_dead_reckons_a_body_axis_turn() -> void:
	var origin := _origin()
	var manager := _manager()
	manager.smoothing_time = 0.0
	manager.timeout = 100.0  # one PDU for a whole circle
	# Heading north at 20 m/s, turning right at 0.1 rad/s (DIS body z is down): a circle of radius 200 m.
	var pdu := _pdu(origin, Vector3i(1, 2, 9), Vector3.ZERO, Basis.IDENTITY, Vector3(0, 0, -20), 7)
	pdu.dr_angular_velocity = Vector3(0, 0, 0.1)
	manager.handle_entity_state(pdu, 0.0)
	var remote: DisRemoteEntity = manager.get_entity(Vector3i(1, 2, 9))
	manager.process_entities(PI / 0.1)  # half way round: the far side of the circle, facing south
	assert_almost_eq(remote.global_position, Vector3(400, 0, 0), Vector3.ONE * 0.05)
	assert_almost_eq(-remote.global_basis.z, Vector3(0, 0, 1), Vector3.ONE * 1e-3)
	manager.process_entities(2 * PI / 0.1)
	assert_almost_eq(remote.global_position, Vector3.ZERO, Vector3.ONE * 0.05, "back at the start")


func test_manager_smooths_corrections() -> void:
	var origin := _origin()
	var manager := _manager()
	manager.smoothing_time = 0.5
	manager.handle_entity_state(_pdu(origin, Vector3i(1, 2, 3), Vector3.ZERO), 0.0)
	var remote: DisRemoteEntity = manager.get_entity(Vector3i(1, 2, 3))
	manager.handle_entity_state(_pdu(origin, Vector3i(1, 2, 3), Vector3(10, 0, 0)), 1.0)
	assert_almost_eq(remote.global_position.x, 0.0, 1e-3, "no jump")
	manager.process_entities(1.25)
	assert_almost_eq(remote.global_position.x, 5.0, 1e-3, "half way")
	manager.process_entities(1.5)
	assert_almost_eq(remote.global_position.x, 10.0, 1e-3, "arrived")
	# Big jumps are not smoothed.
	manager.handle_entity_state(_pdu(origin, Vector3i(1, 2, 3), Vector3(5000, 0, 0)), 2.0)
	assert_almost_eq(remote.global_position.x, 5000.0, 1e-2)


func test_manager_moves_evenly_through_updates() -> void:
	# A fast entity reported once per second, shown every 0.1 s: an update must not make it stall or jump,
	# even though the node still holds the previous frame's pose when the update arrives.
	var origin := _origin()
	var manager := _manager()
	manager.smoothing_time = 1.0
	var velocity := Vector3(190, 0, -130)
	var start := Vector3(33000, 10000, -30000)
	var previous := Vector3.ZERO
	for step in 31:
		var t := step * 0.1
		if step % 10 == 0:
			manager.handle_entity_state(_pdu(origin, Vector3i(3, 1, 1), start + velocity * t, Basis.IDENTITY,
					velocity), t)
		manager.process_entities(t)
		var p: Vector3 = manager.get_entity(Vector3i(3, 1, 1)).global_position
		if step > 0:
			assert_almost_eq((p - previous).length() / 0.1, velocity.length(), 0.5, "speed at %.1f s" % t)
		previous = p


func test_manager_removes_timed_out_and_deactivated() -> void:
	var origin := _origin()
	var manager := _manager()
	manager.timeout = 12.0
	manager.handle_entity_state(_pdu(origin, Vector3i(1, 1, 1), Vector3.ZERO), 0.0)
	manager.handle_entity_state(_pdu(origin, Vector3i(1, 1, 2), Vector3.ZERO), 0.0)
	manager.handle_entity_state(_pdu(origin, Vector3i(1, 1, 2), Vector3.ZERO), 10.0)
	manager.process_entities(12.5)
	assert_eq(manager.get_entity_count(), 1)
	assert_eq(_events[-1][0], "removed")
	assert_eq(_events[-1][2], "timeout")
	var gone := _pdu(origin, Vector3i(1, 1, 2), Vector3.ZERO)
	gone.appearance = DisEntity.APPEARANCE_DEACTIVATED
	manager.handle_entity_state(gone, 13.0)
	assert_eq(manager.get_entity_count(), 0)
	assert_eq(_events[-1][2], "deactivated")


func test_manager_uses_type_map() -> void:
	var origin := _origin()
	var manager := _manager()
	var scene := PackedScene.new()
	var model := MeshInstance3D.new()
	model.name = "TankModel"
	scene.pack(model)
	model.free()
	var mapping := DisEntityTypeMapping.new()
	mapping.entity_type = "1:1:*"
	mapping.scene = scene
	var type_map := DisEntityTypeMap.new()
	type_map.mappings = [mapping]
	manager.type_map = type_map
	var tank := _pdu(origin, Vector3i(1, 1, 1), Vector3.ZERO)
	tank.entity_type = PackedInt32Array([1, 1, 225, 1, 1, 0, 0])
	manager.handle_entity_state(tank, 0.0)
	var aircraft := _pdu(origin, Vector3i(1, 1, 2), Vector3.ZERO)
	aircraft.entity_type = PackedInt32Array([1, 2, 225, 1, 1, 0, 0])
	manager.handle_entity_state(aircraft, 0.0)
	assert_not_null(manager.get_entity(Vector3i(1, 1, 1)).get_node_or_null("TankModel"))
	assert_eq(manager.get_entity(Vector3i(1, 1, 2)).get_child_count(), 0, "no mapping, no default scene")


func test_manager_warns_about_shared_entity_ids() -> void:
	var origin := _origin()
	var manager := _manager()
	# Two senders with the same ID, 30 km apart, updating in turn.
	for i in 4:
		var x := 0.0 if i % 2 == 0 else 30000.0
		manager.handle_entity_state(_pdu(origin, Vector3i(3, 1, 1), Vector3(x, 0, 0)), i * 0.5)
	assert_push_warning("same entity ID")
	# One real teleport is not reported.
	manager.handle_entity_state(_pdu(origin, Vector3i(3, 1, 2), Vector3.ZERO), 0.0)
	manager.handle_entity_state(_pdu(origin, Vector3i(3, 1, 2), Vector3(5000, 0, 0)), 1.0)
	manager.handle_entity_state(_pdu(origin, Vector3i(3, 1, 2), Vector3(5000, 0, 0)), 2.0)
	assert_push_warning_count(1)


## Ground for clamping tests: a plane rising 1 m per 10 m eastwards, 100 m at x = 0, no ground beyond |x| = 1000.
func _ground() -> Node3D:
	var script := GDScript.new()
	script.source_code = ("extends Node3D\nfunc height_at(x: float, _z: float) -> float:\n"
			+ "\treturn NAN if absf(x) > 1000.0 else 100.0 + 0.1 * x\n")
	script.reload()
	var ground := Node3D.new()
	ground.set_script(script)
	add_child_autofree(ground)
	return ground


func _typed_pdu(origin: DisGeoOrigin, id: int, position: Vector3, entity_type: Array,
		basis := Basis.IDENTITY) -> DisEntityStatePdu:
	var pdu := _pdu(origin, Vector3i(1, 1, id), position, basis)
	pdu.entity_type = PackedInt32Array(entity_type)
	return pdu


func test_manager_clamps_land_entities_to_ground() -> void:
	var origin := _origin()
	var manager := _manager()
	manager.ground_path = manager.get_path_to(_ground())
	manager.ground_align = false
	manager.handle_entity_state(_typed_pdu(origin, 1, Vector3(200, 5, 0), [1, 1, 225, 1, 1, 0, 0]), 0.0)  # tank
	manager.handle_entity_state(_typed_pdu(origin, 2, Vector3(200, 500, 0), [1, 2, 225, 1, 1, 0, 0]), 0.0)  # aircraft
	manager.handle_entity_state(_typed_pdu(origin, 3, Vector3(-300, 0, 50), [3, 1, 225, 1, 0, 0, 0]), 0.0)  # soldier
	manager.handle_entity_state(_typed_pdu(origin, 4, Vector3(5000, 7, 0), [1, 1, 225, 1, 1, 0, 0]), 0.0)  # off map
	var tank: DisRemoteEntity = manager.get_entity(Vector3i(1, 1, 1))
	assert_almost_eq(tank.global_position, Vector3(200, 120, 0), Vector3.ONE * 1e-3)
	assert_true(manager.is_ground_clamped(tank))
	var aircraft: DisRemoteEntity = manager.get_entity(Vector3i(1, 1, 2))
	assert_almost_eq(aircraft.global_position.y, 500.0, 1e-2, "air entities keep their height")
	assert_false(manager.is_ground_clamped(aircraft))
	assert_almost_eq(manager.get_entity(Vector3i(1, 1, 3)).global_position.y, 70.0, 1e-3, "life form")
	assert_almost_eq(manager.get_entity(Vector3i(1, 1, 4)).global_position.y, 7.0, 0.1, "NAN: reported height kept")
	# Dead reckoning moves the tank east and up the slope.
	manager.smoothing_time = 0.0
	var moving := _typed_pdu(origin, 1, Vector3(200, 5, 0), [1, 1, 225, 1, 1, 0, 0])
	moving.linear_velocity = origin.local_vector_to_ecef(Vector3(10, 0, 0))
	manager.handle_entity_state(moving, 1.0)
	manager.process_entities(3.0)
	assert_almost_eq(tank.global_position, Vector3(220, 122, 0), Vector3.ONE * 1e-2)
	manager.ground_clamp = DisEntityManager.GROUND_CLAMP_ALL
	manager.process_entities(3.0)
	assert_almost_eq(aircraft.global_position.y, 120.0, 1e-3, "ALL clamps aircraft too")
	manager.ground_clamp = DisEntityManager.GROUND_CLAMP_DISABLED
	manager.process_entities(3.0)
	assert_almost_eq(tank.global_position.y, 5.0, 0.01, "disabled")


func test_manager_aligns_to_slope_keeping_heading() -> void:
	var origin := _origin()
	var manager := _manager()
	manager.ground_path = manager.get_path_to(_ground())
	# Facing east (up the slope) and facing north (across it).
	var east := Basis.looking_at(Vector3.RIGHT, Vector3.UP)
	manager.handle_entity_state(_typed_pdu(origin, 1, Vector3(0, 0, 0), [1, 1, 225, 1, 1, 0, 0], east), 0.0)
	manager.handle_entity_state(_typed_pdu(origin, 2, Vector3(0, 0, 0), [1, 1, 225, 1, 1, 0, 0]), 0.0)
	var slope_up := Vector3(-0.1, 1, 0).normalized()  # surface normal of y = 100 + 0.1 x
	var climbing: DisRemoteEntity = manager.get_entity(Vector3i(1, 1, 1))
	assert_almost_eq(climbing.global_basis.y, slope_up, Vector3.ONE * 1e-4, "up = ground normal")
	assert_almost_eq(-climbing.global_basis.z, Vector3(1, 0.1, 0).normalized(), Vector3.ONE * 1e-4, "nose up")
	var across: DisRemoteEntity = manager.get_entity(Vector3i(1, 1, 2))
	assert_almost_eq(-across.global_basis.z, Vector3(0, 0, -1), Vector3.ONE * 1e-4, "heading north kept")
	assert_almost_eq(across.global_basis.y, slope_up, Vector3.ONE * 1e-4, "rolled")


func test_manager_warns_about_ground_without_height_at() -> void:
	var origin := _origin()
	var manager := _manager()
	var wrong := Node3D.new()
	add_child_autofree(wrong)
	manager.ground_path = manager.get_path_to(wrong)
	manager.handle_entity_state(_typed_pdu(origin, 1, Vector3(0, 3, 0), [1, 1, 225, 1, 1, 0, 0]), 0.0)
	assert_push_warning("has no height_at")
	assert_almost_eq(manager.get_entity(Vector3i(1, 1, 1)).global_position.y, 3.0, 1e-3)


# --- Both over UDP ---------------------------------------------------------------------------------------

func test_entity_to_manager_over_udp() -> void:
	var origin := _origin()
	var sender := DisNetwork.new()
	sender.port = 3150
	sender.mode = DisNetwork.MODE_UNICAST
	sender.destination_address = "127.0.0.1"
	sender.destination_port = 3151
	sender.site_id = 10
	add_child_autofree(sender)
	var receiver := DisNetwork.new()
	receiver.port = 3151
	receiver.site_id = 20
	add_child_autofree(receiver)

	var manager := DisEntityManager.new()  # auto_update on: real time
	manager.entity_removed.connect(func(e, reason): _events.append(["removed", e, reason]))
	add_child_autofree(manager)
	manager.network_path = manager.get_path_to(receiver)

	var entity := DisEntity.new()  # auto_update on
	entity.marking = "UDP"
	entity.position = Vector3(30, 5, -40)
	add_child(entity)
	entity.network_path = entity.get_path_to(sender)

	var deadline := Time.get_ticks_msec() + 3000
	while manager.get_entity_count() == 0 and Time.get_ticks_msec() < deadline:
		await wait_physics_frames(1)
	assert_eq(manager.get_entity_count(), 1, "remote entity appeared")
	var remote: DisRemoteEntity = manager.get_entities()[0]
	assert_eq(remote.entity_id.x, 10, "site from the sending network")
	assert_eq(remote.marking, "UDP")
	assert_almost_eq(remote.global_position, Vector3(30, 5, -40), Vector3.ONE * 1e-3)

	entity.queue_free()  # sends a deactivated PDU on exit
	deadline = Time.get_ticks_msec() + 3000
	while manager.get_entity_count() > 0 and Time.get_ticks_msec() < deadline:
		await wait_process_frames(1)
	assert_eq(manager.get_entity_count(), 0, "removed when the publisher left")
	assert_eq(_events[-1][2] if _events.size() > 0 else "", "deactivated")
