extends SceneTree
## Writes DisProbe.encode_test_entity_state() to the file given after "--".
## <godot> --headless --path . -s res://tools/dump_probe.gd -- <out.bin>

func _init() -> void:
	var out := OS.get_cmdline_user_args()[0]
	print(DisProbe.get_version())
	var bytes := DisProbe.encode_test_entity_state()
	print("bytes: ", bytes.size())
	var file := FileAccess.open(out, FileAccess.WRITE)
	file.store_buffer(bytes)
	file.close()
	quit()
