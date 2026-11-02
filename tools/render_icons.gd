extends SceneTree
## Renders icon.svg to docs/images/icon.png (128 px, the Asset Library icon) and, with an output path, a sheet of
## the editor icons at 4x on a dark background, to look at them.
##   <godot> --headless --path . -s res://tools/render_icons.gd [-- sheet.png]


func _initialize() -> void:
	var icon := Image.new()
	icon.load_svg_from_string(FileAccess.get_file_as_string("res://icon.svg"), 1.0)
	icon.save_png("res://docs/images/icon.png")
	print("docs/images/icon.png ", icon.get_size())
	var args := OS.get_cmdline_user_args()
	if not args.is_empty():
		var names := Array(DirAccess.get_files_at("res://addons/dis/icons")).filter(func(f): return f.ends_with(".svg"))
		var sheet := Image.create(80 * names.size(), 80, false, Image.FORMAT_RGBA8)
		sheet.fill(Color("#363d4a"))
		for i in names.size():
			var image := Image.new()
			image.load_svg_from_string(FileAccess.get_file_as_string("res://addons/dis/icons/" + names[i]), 4.0)
			sheet.blend_rect(image, Rect2i(Vector2i.ZERO, image.get_size()), Vector2i(80 * i + 8, 8))
		sheet.save_png(args[0])
		print(", ".join(names))
	quit()
