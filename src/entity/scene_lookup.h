#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace dis {

// Finds the node of type T that `self` works with: the one at `path` if it is set, otherwise the first node in
// `group` (DisNetwork joins "dis_network", DisGeoOrigin joins "dis_geo_origin"). Null if there is none.
template <typename T>
T *find_node(godot::Node *self, const godot::NodePath &path, const godot::StringName &group) {
	if (!self->is_inside_tree()) {
		return nullptr;
	}
	if (!path.is_empty()) {
		return godot::Object::cast_to<T>(self->get_node_or_null(path));
	}
	return godot::Object::cast_to<T>(self->get_tree()->get_first_node_in_group(group));
}

} // namespace dis
