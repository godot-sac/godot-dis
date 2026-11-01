extends SceneTree
## Debug helper: decodes every supported PDU of a capture, encodes it again and prints where the bytes differ.
##   <godot> --headless --path . -s res://tools/diff_capture.gd -- <capture> [max reports]


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var recording := DisRecording.new()
	if args.is_empty() or recording.load(args[0]) != OK:
		printerr("usage: -- <capture file>")
		quit(1)
		return
	var limit := int(args[1]) if args.size() > 1 else 5
	var reported := 0
	var checked := 0
	for i in recording.get_packet_count():
		var bytes := recording.get_bytes(i)
		var pdu := DisPdu.decode(bytes)
		if pdu == null:
			continue
		checked += 1
		var encoded := pdu.encode()
		var original := bytes.slice(0, DisPdu.get_effective_length(bytes)) if bytes[8] or bytes[9] else bytes
		if encoded == original:
			continue
		reported += 1
		if reported > limit:
			continue
		var first := -1
		for j in mini(encoded.size(), original.size()):
			if encoded[j] != original[j]:
				first = j
				break
		print("#%d %s: original %d bytes (length field %d), ours %d; first difference at %d" % [i, pdu.get_class(),
				bytes.size(), (bytes[8] << 8) | bytes[9], encoded.size(), first])
		var from := maxi(first - 4, 0)
		print("  original %s" % original.slice(from, from + 24).hex_encode())
		print("  ours     %s" % encoded.slice(from, from + 24).hex_encode())
	print("%d decoded, %d re-encode differently" % [checked, reported])
	quit(0)
