#include "editor_first_load_guard.h"

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable_custom.hpp>
#include <godot_cpp/variant/dictionary.hpp>

using namespace godot;

namespace {

// Any root signal works: the guard is never called, only its destruction (with the root's connections) matters.
const char *const GUARD_SIGNAL = "renamed";

class FirstLoadGuard;
FirstLoadGuard *active_guard = nullptr;
bool removing_guard = false;

class FirstLoadGuard : public CallableCustom {
	static bool compare_equal(const CallableCustom *p_a, const CallableCustom *p_b) { return p_a == p_b; }
	static bool compare_less(const CallableCustom *p_a, const CallableCustom *p_b) { return p_a < p_b; }

public:
	uint32_t hash() const override { return uint32_t(reinterpret_cast<uintptr_t>(this)); }
	String get_as_text() const override { return "EditorFirstLoadGuard"; }
	CompareEqualFunc get_compare_equal_func() const override { return compare_equal; }
	CompareLessFunc get_compare_less_func() const override { return compare_less; }
	// No target object; without this the callable counts as invalid and connect() refuses it.
	bool is_valid() const override { return true; }
	ObjectID get_object() const override { return ObjectID(); }
	void call(const Variant **, int, Variant &, GDExtensionCallError &r_call_error) const override {
		r_call_error.error = GDEXTENSION_CALL_OK;
	}

	~FirstLoadGuard() override {
		active_guard = nullptr;
		if (!removing_guard) {
			// The root window is being freed at shutdown: unregister before the editor's queued doc generation runs.
			ClassDB::deinitialize(GDEXTENSION_INITIALIZATION_EDITOR);
			ClassDB::deinitialize(GDEXTENSION_INITIALIZATION_SCENE);
		}
	}
};

Window *get_root() {
	SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
	return tree ? tree->get_root() : nullptr;
}

} // namespace

void install_editor_first_load_guard() {
	if (active_guard || !Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	// At startup the extensions are initialized before the main loop (and the display server) exist: nothing to guard.
	Window *root = get_root();
	if (root == nullptr) {
		return;
	}
	// The editor treats a display server that cannot draw as command line mode (--headless).
	if (DisplayServer::get_singleton()->window_can_draw()) {
		return;
	}
	active_guard = memnew(FirstLoadGuard);
	root->connect(GUARD_SIGNAL, Callable(active_guard));
}

void remove_editor_first_load_guard() {
	if (active_guard == nullptr) {
		return;
	}
	Window *root = get_root();
	if (root == nullptr) {
		return;
	}
	removing_guard = true;
	{
		// The connection list holds references too; the guard is freed once the last one goes out of scope.
		Array connections = root->get_signal_connection_list(GUARD_SIGNAL);
		for (int i = 0; i < connections.size(); i++) {
			Callable callable = Dictionary(connections[i])["callable"];
			if (callable.get_custom() == active_guard) {
				root->disconnect(GUARD_SIGNAL, callable);
				break;
			}
		}
	}
	removing_guard = false;
}
