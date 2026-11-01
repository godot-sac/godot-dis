extends GutTest
## DisRadioMonitor: who is on the air, from Transmitter and Signal PDUs.

const CAPTURE := "res://tools/.cache/captures/DIS_signal_and_transmitter.pcapng"

var _monitor: DisRadioMonitor
var _events: Array = []


func before_each() -> void:
	_events.clear()
	_monitor = DisRadioMonitor.new()
	_monitor.signal_hold = 0.15
	_monitor.timeout = 0.4
	add_child_autofree(_monitor)
	_monitor.radio_added.connect(func(id, radio): _events.append(["added", id, radio]))
	_monitor.radio_removed.connect(func(id, radio): _events.append(["removed", id, radio]))
	_monitor.transmission_started.connect(func(id, radio, frequency): _events.append(["started", id, radio, frequency]))
	_monitor.transmission_ended.connect(func(id, radio): _events.append(["ended", id, radio]))


func _transmitter(state: int) -> DisTransmitterPdu:
	var pdu := DisTransmitterPdu.new()
	pdu.radio_reference_id = Vector3i(1, 1, 4)
	pdu.radio_number = 2
	pdu.transmit_state = state
	pdu.frequency = 243000000
	return pdu


func _signal() -> DisSignalPdu:
	var pdu := DisSignalPdu.new()
	pdu.radio_reference_id = Vector3i(1, 1, 4)
	pdu.radio_number = 2
	pdu.set_data_bytes(PackedByteArray([0, 0, 0, 0]))
	return pdu


## Waits `seconds` of wall-clock time (the monitor runs on real time; wait_seconds() can be shorter headless).
func _wait_real(seconds: float) -> void:
	var until := Time.get_ticks_msec() + int(seconds * 1000)
	while Time.get_ticks_msec() < until:
		await wait_process_frames(1)


func _kinds() -> Array:
	return _events.map(func(e): return e[0])


func test_transmit_state_and_signals() -> void:
	_monitor.handle_pdu(_transmitter(DisTransmitterPdu.TRANSMIT_ON_NOT_TRANSMITTING))
	assert_eq(_kinds(), ["added"], "a radio that is on but silent")
	assert_false(_monitor.is_transmitting(Vector3i(1, 1, 4)))
	assert_eq(_monitor.get_radios()[0].frequency, 243000000)

	_monitor.handle_pdu(_signal())
	assert_eq(_events.back(), ["started", Vector3i(1, 1, 4), 2, 243000000], "a Signal PDU means it is transmitting")
	assert_eq(_monitor.get_transmitting_entities(), [Vector3i(1, 1, 4)])
	await _wait_real(0.25)
	_monitor.update()
	assert_eq(_events.back()[0], "ended", "silent for longer than signal_hold")

	_monitor.handle_pdu(_transmitter(DisTransmitterPdu.TRANSMIT_ON_AND_TRANSMITTING))
	assert_true(_monitor.is_transmitting(Vector3i(1, 1, 4)), "keyed by the Transmitter PDU alone")
	await _wait_real(0.25)
	_monitor.update()
	assert_true(_monitor.is_transmitting(Vector3i(1, 1, 4)), "stays keyed without Signal PDUs")
	_monitor.handle_pdu(_transmitter(DisTransmitterPdu.TRANSMIT_OFF))
	assert_false(_monitor.is_transmitting(Vector3i(1, 1, 4)))

	await _wait_real(0.5)
	_monitor.update()
	assert_eq(_events.back(), ["removed", Vector3i(1, 1, 4), 2], "forgotten after the timeout")
	assert_eq(_monitor.get_radios().size(), 0)


func test_radios_in_the_wireshark_capture() -> void:
	if not FileAccess.file_exists(CAPTURE):
		pending("DIS_signal_and_transmitter.pcapng not downloaded (tools/fetch_captures.py)")
		return
	var network := DisNetwork.new()
	network.autostart = false
	add_child_autofree(network)
	_monitor.timeout = 60.0
	_monitor.network_path = _monitor.get_path_to(network)
	var player := DisPlayer.new()
	add_child_autofree(player)
	player.network_path = player.get_path_to(network)
	assert_eq(player.load(CAPTURE), OK)
	player.advance(3.0)
	var radios := _monitor.get_radios()
	assert_gt(radios.size(), 0, "radios found in the capture")
	assert_true(_kinds().has("started"), "someone was transmitting")
	assert_eq(_monitor.get_transmitting_entities().size(), radios.size(), "the capture is one long transmission")
