#include "record/dis_player.h"

#include <cstring>
#include <unordered_map>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "entity/dis_entity.h"
#include "entity/dis_entity_manager.h"
#include "entity/scene_lookup.h"
#include "network/dis_network.h"
#include "pdu/dis_entity_state_pdu.h"
#include "pdu/dis_pdu.h"

using namespace godot;
using dis::capture::Packet;

void DisPlayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load", "file_path"), &DisPlayer::load);
	ClassDB::bind_method(D_METHOD("play"), &DisPlayer::play);
	ClassDB::bind_method(D_METHOD("pause"), &DisPlayer::pause);
	ClassDB::bind_method(D_METHOD("stop"), &DisPlayer::stop);
	ClassDB::bind_method(D_METHOD("seek", "time"), &DisPlayer::seek);
	ClassDB::bind_method(D_METHOD("advance", "seconds"), &DisPlayer::advance);
	ClassDB::bind_method(D_METHOD("is_playing"), &DisPlayer::is_playing);
	ClassDB::bind_method(D_METHOD("get_position"), &DisPlayer::get_position);
	ClassDB::bind_method(D_METHOD("get_duration"), &DisPlayer::get_duration);

	DIS_BIND_PROPERTY(DisPlayer, OBJECT, recording);
	DIS_BIND_PROPERTY(DisPlayer, STRING, path);
	DIS_BIND_PROPERTY(DisPlayer, NODE_PATH, network_path);
	DIS_BIND_PROPERTY(DisPlayer, NODE_PATH, entity_manager_path);
	ClassDB::bind_method(D_METHOD("set_output", "value"), &DisPlayer::set_output);
	ClassDB::bind_method(D_METHOD("get_output"), &DisPlayer::get_output);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "output", PROPERTY_HINT_ENUM, "Local:1,Network:2,Both:3"), "set_output",
			"get_output");
	ClassDB::bind_method(D_METHOD("set_speed", "value"), &DisPlayer::set_speed);
	ClassDB::bind_method(D_METHOD("get_speed"), &DisPlayer::get_speed);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed", PROPERTY_HINT_RANGE, "0.01,100,0.01,or_greater"), "set_speed",
			"get_speed");
	DIS_BIND_PROPERTY(DisPlayer, BOOL, loop);
	DIS_BIND_PROPERTY(DisPlayer, BOOL, autoplay);
	DIS_BIND_PROPERTY(DisPlayer, INT, exercise_id_override);

	ADD_SIGNAL(MethodInfo("packet_played", PropertyInfo(Variant::INT, "index"), PropertyInfo(Variant::PACKED_BYTE_ARRAY, "bytes")));
	ADD_SIGNAL(MethodInfo("seeked", PropertyInfo(Variant::FLOAT, "position")));
	ADD_SIGNAL(MethodInfo("finished"));

	BIND_ENUM_CONSTANT(OUTPUT_LOCAL);
	BIND_ENUM_CONSTANT(OUTPUT_NETWORK);
	BIND_ENUM_CONSTANT(OUTPUT_BOTH);
}

void DisPlayer::_notification(int p_what) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	switch (p_what) {
		case NOTIFICATION_READY:
			set_process(true);
			if (recording.is_null() && !path.is_empty()) {
				load(path);
			}
			if (autoplay && recording.is_valid()) {
				play();
			}
			break;
		case NOTIFICATION_PROCESS:
			if (playing) {
				advance(get_process_delta_time() * speed);
			}
			break;
	}
}

DisNetwork *DisPlayer::get_network() {
	return dis::find_node<DisNetwork>(this, network_path, "dis_network");
}

DisEntityManager *DisPlayer::get_entity_manager() {
	if (!is_inside_tree()) {
		return nullptr;
	}
	if (!entity_manager_path.is_empty()) {
		return Object::cast_to<DisEntityManager>(get_node_or_null(entity_manager_path));
	}
	return dis::find_first_of_class<DisEntityManager>(get_tree()->get_root());
}

Error DisPlayer::load(const String &file_path) {
	Ref<DisRecording> loaded;
	loaded.instantiate();
	const Error error = loaded->load(file_path, 0);
	if (error != OK) {
		UtilityFunctions::push_error("DisPlayer: cannot load ", file_path, ": ", loaded->get_last_error());
		return error;
	}
	path = file_path;
	set_recording(loaded);
	return OK;
}

void DisPlayer::set_recording(const Ref<DisRecording> &value) {
	playing = false;
	recording = value;
	position = 0.0;
	next_index = 0;
	update_manager_clock();
}

double DisPlayer::get_duration() const {
	return recording.is_valid() ? recording->get_duration() : 0.0;
}

void DisPlayer::set_speed(double value) {
	speed = value > 0.0 ? value : 0.0;
	update_manager_clock();
}

void DisPlayer::update_manager_clock() {
	if ((output & OUTPUT_LOCAL) == 0) {
		return;
	}
	DisEntityManager *manager = get_entity_manager();
	if (manager != nullptr) {
		manager->set_time_scale(playing ? speed : 0.0);
	}
}

void DisPlayer::play() {
	ERR_FAIL_COND_MSG(recording.is_null(), "DisPlayer: no recording");
	if (next_index >= recording->get_packet_count()) {
		seek(0.0);
	}
	playing = true;
	update_manager_clock();
}

void DisPlayer::pause() {
	playing = false;
	update_manager_clock();
}

void DisPlayer::stop() {
	playing = false;
	seek(0.0);
}

void DisPlayer::seek(double time) {
	if (recording.is_null()) {
		return;
	}
	position = time < 0.0 ? 0.0 : (time > get_duration() ? get_duration() : time);
	next_index = recording->find_index(position);
	update_manager_clock();
	restore_entities();
	emit_signal("seeked", position);
}

void DisPlayer::advance(double seconds) {
	if (recording.is_null()) {
		return;
	}
	position += seconds;
	// Signal handlers of the played packets may change or clear the recording, seek, or set another one: keep
	// this one alive and look at it again after every packet.
	const Ref<DisRecording> current = recording;
	const double start = current->get_start_time();
	while (recording == current && next_index < current->get_packet_count() &&
			current->get_packets()[next_index].time - start <= position) {
		const int index = next_index;
		play_packet(index);
		if (next_index == index) { // unless a handler seeked
			next_index++;
		}
	}
	if (recording != current) {
		return;
	}
	const int count = current->get_packet_count();
	if (next_index >= count && position >= get_duration()) {
		position = get_duration();
		if (loop && count > 0) {
			seek(0.0);
		} else if (playing) {
			playing = false;
			update_manager_clock();
			emit_signal("finished");
		}
	}
}

void DisPlayer::play_packet(int index) {
	const Packet &packet = recording->get_packets()[index];
	PackedByteArray bytes;
	bytes.resize(static_cast<int64_t>(packet.data.size()));
	if (!packet.data.empty()) {
		memcpy(bytes.ptrw(), packet.data.data(), packet.data.size());
	}
	if (exercise_id_override >= 0 && bytes.size() > 1) {
		bytes[1] = static_cast<uint8_t>(exercise_id_override);
	}
	DisNetwork *network = get_network();
	if (network != nullptr) {
		if (output & OUTPUT_LOCAL) {
			network->inject_packet(bytes, String::utf8(packet.source_ip.c_str()), packet.source_port, false);
		}
		if ((output & OUTPUT_NETWORK) && network->is_running()) {
			network->send_bytes(bytes);
		}
	}
	emit_signal("packet_played", index, bytes);
}

void DisPlayer::restore_entities() {
	if ((output & OUTPUT_LOCAL) == 0) {
		return;
	}
	DisEntityManager *manager = get_entity_manager();
	if (manager == nullptr) {
		return;
	}
	manager->clear();
	const std::vector<Packet> &packets = recording->get_packets();
	const double start = recording->get_start_time();
	const int first = recording->find_index(position - manager->get_timeout());
	// Last Entity State PDU of each entity before the new position.
	std::unordered_map<uint64_t, int> latest;
	for (int i = first; i < next_index; i++) {
		const std::vector<uint8_t> &data = packets[i].data;
		if (data.size() >= 18 && data[2] == DisPdu::PDU_TYPE_ENTITY_STATE) {
			const uint64_t key = (static_cast<uint64_t>(data[12]) << 40) | (static_cast<uint64_t>(data[13]) << 32) |
					(static_cast<uint64_t>(data[14]) << 24) | (static_cast<uint64_t>(data[15]) << 16) |
					(static_cast<uint64_t>(data[16]) << 8) | data[17];
			latest[key] = i;
		}
	}
	const double clock = manager->get_clock();
	for (const auto &[key, index] : latest) {
		const std::vector<uint8_t> &data = packets[index].data;
		PackedByteArray bytes;
		bytes.resize(static_cast<int64_t>(data.size()));
		memcpy(bytes.ptrw(), data.data(), data.size());
		const Ref<DisEntityStatePdu> pdu = DisEntityStatePdu::decode(bytes);
		if (pdu.is_null() || (pdu->get_appearance() & DisEntity::APPEARANCE_DEACTIVATED)) {
			continue;
		}
		manager->apply_entity_state(pdu, clock - (position - (packets[index].time - start)));
	}
}
