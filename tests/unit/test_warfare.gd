extends GutTest
## DisEntity.fire() / detonate() and Fire / Detonation over loopback UDP (ports 315x).

const TANK_ROUND := [2, 9, 225, 2, 14, 0, 0]

var _origin: DisGeoOrigin
var _network: DisNetwork
var _receiver: DisNetwork
var _entity: DisEntity


func before_each() -> void:
	_origin = DisGeoOrigin.new()
	_origin.set_origin_geodetic(39.9334, 32.8597, 938.0)
	add_child_autofree(_origin)
	_network = DisNetwork.new()
	_network.mode = DisNetwork.MODE_UNICAST
	_network.port = 3150
	_network.destination_address = "127.0.0.1"
	_network.destination_port = 3151
	_network.site_id = 3
	_network.application_id = 4
	add_child_autofree(_network)
	_receiver = DisNetwork.new()
	_receiver.mode = DisNetwork.MODE_UNICAST
	_receiver.port = 3151
	_receiver.site_id = 9
	add_child_autofree(_receiver)
	_entity = DisEntity.new()
	_entity.entity_id = Vector3i(0, 0, 12)
	_entity.publishing = false
	add_child_autofree(_entity)
	_entity.network_path = _entity.get_path_to(_network)
	_entity.origin_path = _entity.get_path_to(_origin)
	_entity.global_position = Vector3(100, 5, -200)


func _wait_for(condition: Callable, seconds: float) -> bool:
	var deadline := Time.get_ticks_msec() + int(seconds * 1000)
	while not condition.call() and Time.get_ticks_msec() < deadline:
		await wait_process_frames(1)
	return condition.call()


func test_create_fire_pdu() -> void:
	var fire := _entity.create_fire_pdu(PackedInt32Array(TANK_ROUND), Vector3i(9, 1, 7), Vector3(0, 10, -900), 2500.0)
	assert_not_null(fire)
	assert_eq(fire.firing_entity_id, Vector3i(3, 4, 12))
	assert_eq(fire.target_entity_id, Vector3i(9, 1, 7))
	assert_eq(fire.munition_type, PackedInt32Array(TANK_ROUND))
	assert_eq(fire.quantity, 1)
	assert_eq(fire.range, 2500.0)
	assert_eq(Vector2i(fire.event_id.x, fire.event_id.y), Vector2i(3, 4), "event ID carries our site / application")
	var location := _origin.ecef_to_local(fire.location_ecef)
	assert_almost_eq(location, Vector3(100, 5, -200), Vector3.ONE * 1e-3, "fired from the entity's position")
	assert_almost_eq(_origin.ecef_vector_to_local(fire.velocity), Vector3(0, 10, -900), Vector3.ONE * 1e-2)
	var second := _entity.create_fire_pdu(PackedInt32Array(TANK_ROUND))
	assert_ne(second.event_id, fire.event_id, "every fire gets a new event number")


func test_detonation_continues_the_fire_event() -> void:
	var fire := _entity.create_fire_pdu(PackedInt32Array(TANK_ROUND), Vector3i(9, 1, 7))
	fire.warhead = 1000
	fire.fuse = 1000
	var detonation := _entity.create_detonation_pdu(Vector3(1000, 0, -3000), DisDetonationPdu.RESULT_ENTITY_IMPACT,
			fire)
	assert_eq(detonation.event_id, fire.event_id)
	assert_eq(detonation.target_entity_id, Vector3i(9, 1, 7), "target from the fire")
	assert_eq(detonation.munition_type, PackedInt32Array(TANK_ROUND))
	assert_eq([detonation.warhead, detonation.fuse, detonation.quantity], [1000, 1000, 1])
	assert_eq(detonation.detonation_result, DisDetonationPdu.RESULT_ENTITY_IMPACT)
	assert_almost_eq(_origin.ecef_to_local(detonation.location_ecef), Vector3(1000, 0, -3000), Vector3.ONE * 1e-3)
	var alone := _entity.create_detonation_pdu(Vector3.ZERO, DisDetonationPdu.RESULT_GROUND_IMPACT)
	assert_ne(alone.event_id, fire.event_id, "without a fire: a new event")


func test_fire_and_detonation_over_the_network() -> void:
	var fires := []
	var detonations := []
	_receiver.fire_received.connect(func(pdu): fires.append(pdu))
	_receiver.detonation_received.connect(func(pdu): detonations.append(pdu))
	var fire := _entity.fire(PackedInt32Array(TANK_ROUND), Vector3i(9, 1, 7), Vector3(0, 0, -900), 3000.0)
	_entity.detonate(Vector3(0, 0, -3000), DisDetonationPdu.RESULT_ENTITY_IMPACT, fire)
	assert_true(await _wait_for(func(): return fires.size() == 1 and detonations.size() == 1, 2.0), "both arrived")
	assert_eq(fires[0].event_id, fire.event_id)
	assert_eq(detonations[0].event_id, fire.event_id)
	assert_eq(fires[0].encode(), fire.encode(), "the PDU arrives unchanged")
	assert_eq(_network.get_statistics().sent, 2)
