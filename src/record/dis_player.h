#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/string.hpp>

#include "record/dis_recording.h"

class DisEntityManager;
class DisNetwork;

// Plays a DisRecording back in time: into the scene through a DisNetwork (OUTPUT_LOCAL: its signals fire as if
// the PDUs had arrived, so a DisEntityManager shows the recorded entities), onto the network (OUTPUT_NETWORK:
// sent again with the network's settings), or both. Speed, pause, seek and loop.
// With an entity manager (entity_manager_path, or the first DisEntityManager in the scene) playback speed also
// scales its dead-reckoning clock, and seek() rebuilds the picture at once: the manager is cleared and every
// entity's last Entity State PDU before the new position is applied, aged by how long before it was sent.
class DisPlayer : public godot::Node {
	GDCLASS(DisPlayer, godot::Node)

public:
	enum Output {
		OUTPUT_LOCAL = 1,
		OUTPUT_NETWORK = 2,
		OUTPUT_BOTH = 3,
	};

private:
	godot::Ref<DisRecording> recording;
	godot::String path;
	godot::NodePath network_path;
	godot::NodePath entity_manager_path;
	int output = OUTPUT_LOCAL;
	double speed = 1.0;
	bool loop = false;
	bool autoplay = false;
	int exercise_id_override = -1;

	bool playing = false;
	double position = 0.0; // seconds since the first packet
	int next_index = 0;

	DisNetwork *get_network();
	DisEntityManager *get_entity_manager();
	void play_packet(int index);
	void update_manager_clock();
	void restore_entities();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	// Loads `file_path` into a new DisRecording. Stops playback.
	godot::Error load(const godot::String &file_path);
	void play();
	void pause();
	// Pauses and rewinds to the start.
	void stop();
	// Jumps to `time` (seconds since the first packet).
	void seek(double time);
	// Plays every packet up to `time` from the current position (used by tests and tools; no wall clock).
	void advance(double seconds);
	bool is_playing() const { return playing; }
	double get_position() const { return position; }
	double get_duration() const;

	void set_recording(const godot::Ref<DisRecording> &value);
	godot::Ref<DisRecording> get_recording() const { return recording; }
	void set_path(const godot::String &value) { path = value; }
	godot::String get_path() const { return path; }
	void set_network_path(const godot::NodePath &value) { network_path = value; }
	godot::NodePath get_network_path() const { return network_path; }
	void set_entity_manager_path(const godot::NodePath &value) { entity_manager_path = value; }
	godot::NodePath get_entity_manager_path() const { return entity_manager_path; }
	void set_output(int value) { output = value; }
	int get_output() const { return output; }
	void set_speed(double value);
	double get_speed() const { return speed; }
	void set_loop(bool value) { loop = value; }
	bool get_loop() const { return loop; }
	void set_autoplay(bool value) { autoplay = value; }
	bool get_autoplay() const { return autoplay; }
	void set_exercise_id_override(int value) { exercise_id_override = value; }
	int get_exercise_id_override() const { return exercise_id_override; }
};

VARIANT_ENUM_CAST(DisPlayer::Output);
