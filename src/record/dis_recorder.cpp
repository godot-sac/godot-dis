#include "record/dis_recorder.h"

#include <cstring>

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/ip.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "entity/scene_lookup.h"
#include "network/dis_network.h"
#include "record/capture_format.h"

using namespace godot;

namespace {
constexpr size_t FLUSH_BYTES = 256 * 1024;

std::string to_std(const String &text) {
	return std::string(text.utf8().get_data());
}

double now() {
	return Time::get_singleton()->get_unix_time_from_system();
}

// This computer's first routable IPv4 address (for the source of our own PDUs in the file).
std::string first_ipv4() {
	const PackedStringArray addresses = IP::get_singleton()->get_local_addresses();
	for (const String &address : addresses) {
		if (address.contains(".") && address.is_valid_ip_address() && !address.begins_with("127.") &&
				!address.begins_with("169.254.")) {
			return to_std(address);
		}
	}
	return "127.0.0.1";
}
} // namespace

void DisRecorder::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start", "file_path"), &DisRecorder::start, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("stop"), &DisRecorder::stop);
	ClassDB::bind_method(D_METHOD("is_recording"), &DisRecorder::is_recording);
	ClassDB::bind_method(D_METHOD("get_packet_count"), &DisRecorder::get_packet_count);
	ClassDB::bind_method(D_METHOD("get_elapsed"), &DisRecorder::get_elapsed);
	ClassDB::bind_method(D_METHOD("get_recording_path"), &DisRecorder::get_recording_path);

	ClassDB::bind_method(D_METHOD("set_network_path", "value"), &DisRecorder::set_network_path);
	ClassDB::bind_method(D_METHOD("get_network_path"), &DisRecorder::get_network_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "network_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "DisNetwork"),
			"set_network_path", "get_network_path");
	ClassDB::bind_method(D_METHOD("set_path", "value"), &DisRecorder::set_path);
	ClassDB::bind_method(D_METHOD("get_path"), &DisRecorder::get_path);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "path", PROPERTY_HINT_SAVE_FILE, "*.pcapng,*.pdulog"), "set_path", "get_path");
	ClassDB::bind_method(D_METHOD("set_format", "value"), &DisRecorder::set_format);
	ClassDB::bind_method(D_METHOD("get_format"), &DisRecorder::get_format);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "format", PROPERTY_HINT_ENUM, "Auto,pcapng,pdulog"), "set_format", "get_format");
	ClassDB::bind_method(D_METHOD("set_record_sent", "value"), &DisRecorder::set_record_sent);
	ClassDB::bind_method(D_METHOD("get_record_sent"), &DisRecorder::get_record_sent);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "record_sent"), "set_record_sent", "get_record_sent");
	ClassDB::bind_method(D_METHOD("set_autostart", "value"), &DisRecorder::set_autostart);
	ClassDB::bind_method(D_METHOD("get_autostart"), &DisRecorder::get_autostart);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autostart"), "set_autostart", "get_autostart");
	ClassDB::bind_method(D_METHOD("set_flush_interval", "value"), &DisRecorder::set_flush_interval);
	ClassDB::bind_method(D_METHOD("get_flush_interval"), &DisRecorder::get_flush_interval);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "flush_interval", PROPERTY_HINT_RANGE, "0.1,60,0.1,suffix:s"),
			"set_flush_interval", "get_flush_interval");

	ADD_SIGNAL(MethodInfo("started", PropertyInfo(Variant::STRING, "path")));
	ADD_SIGNAL(MethodInfo("stopped", PropertyInfo(Variant::STRING, "path"), PropertyInfo(Variant::INT, "packet_count")));
}

void DisRecorder::_notification(int p_what) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	switch (p_what) {
		case NOTIFICATION_READY:
			set_process(true);
			if (autostart) {
				// Deferred, so that a DisNetwork later in the tree has opened its socket.
				call_deferred("start", String());
			}
			break;
		case NOTIFICATION_PROCESS:
			if (is_recording() && now() - last_flush >= flush_interval) {
				flush();
			}
			break;
		case NOTIFICATION_EXIT_TREE:
		case NOTIFICATION_PREDELETE:
			stop();
			break;
	}
}

DisNetwork *DisRecorder::get_network() {
	return dis::find_node<DisNetwork>(this, network_path, "dis_network");
}

Error DisRecorder::start(const String &file_path) {
	stop();
	DisNetwork *network = get_network();
	ERR_FAIL_NULL_V_MSG(network, ERR_UNCONFIGURED, "DisRecorder: no DisNetwork in the scene");
	recording_path = file_path.is_empty() ? path : file_path;
	const String directory = recording_path.get_base_dir();
	if (!directory.is_empty() && !DirAccess::dir_exists_absolute(directory)) {
		DirAccess::make_dir_recursive_absolute(directory);
	}
	file = FileAccess::open(recording_path, FileAccess::WRITE);
	if (file.is_null()) {
		const Error error = FileAccess::get_open_error();
		UtilityFunctions::push_error("DisRecorder: cannot write ", recording_path, ": ", UtilityFunctions::error_string(error));
		return error;
	}
	pdulog = format == DisRecording::FORMAT_PDULOG ||
			(format == DisRecording::FORMAT_AUTO &&
					dis::capture::format_from_name(to_std(recording_path)) == dis::capture::Format::PDULOG);
	start_time = now();
	last_flush = start_time;
	packet_count = 0;
	byte_count = 0;
	buffer.clear();
	local_ip = first_ipv4();
	network_port = network->get_port();
	switch (network->get_mode()) {
		case DisNetwork::MODE_MULTICAST:
			receive_destination = to_std(network->get_multicast_group());
			break;
		case DisNetwork::MODE_BROADCAST:
			receive_destination = to_std(network->get_destination_address());
			break;
		default:
			receive_destination = local_ip;
			break;
	}
	if (pdulog) {
		const std::string header = dis::capture::pdulog_header(start_time, "godot-dis");
		buffer.insert(buffer.end(), header.begin(), header.end());
	} else {
		buffer = dis::capture::pcapng_file_header("godot-dis");
	}
	flush();
	network->add_tap(this, this);
	network_id = ObjectID(network->get_instance_id());
	emit_signal("started", recording_path);
	return OK;
}

void DisRecorder::stop() {
	if (!is_recording()) {
		return;
	}
	DisNetwork *network = Object::cast_to<DisNetwork>(ObjectDB::get_instance(network_id));
	if (network != nullptr) {
		network->remove_tap(this);
	}
	network_id = ObjectID();
	flush();
	file->close();
	file.unref();
	if (!is_queued_for_deletion() && is_inside_tree()) {
		emit_signal("stopped", recording_path, packet_count);
	}
}

double DisRecorder::get_elapsed() const {
	return is_recording() ? now() - start_time : 0.0;
}

void DisRecorder::flush() {
	if (file.is_null()) {
		return;
	}
	if (!buffer.empty()) {
		PackedByteArray bytes;
		bytes.resize(static_cast<int64_t>(buffer.size()));
		memcpy(bytes.ptrw(), buffer.data(), buffer.size());
		file->store_buffer(bytes);
		buffer.clear();
	}
	file->flush();
	last_flush = now();
}

void DisRecorder::tap_packet(const PackedByteArray &bytes, const String &ip, int port, bool outgoing, bool own) {
	if (!is_recording() || (outgoing && !record_sent) || (!outgoing && own && record_sent)) {
		return;
	}
	dis::capture::Packet packet;
	packet.time = now();
	packet.data.assign(bytes.ptr(), bytes.ptr() + bytes.size());
	if (outgoing) {
		packet.source_ip = local_ip;
		packet.source_port = static_cast<uint16_t>(network_port);
		packet.destination_ip = to_std(ip);
		packet.destination_port = static_cast<uint16_t>(port);
	} else {
		packet.source_ip = to_std(ip);
		packet.source_port = static_cast<uint16_t>(port);
		packet.destination_ip = receive_destination;
		packet.destination_port = static_cast<uint16_t>(network_port);
	}
	if (pdulog) {
		const std::string line = dis::capture::pdulog_line(packet, start_time);
		buffer.insert(buffer.end(), line.begin(), line.end());
	} else {
		const std::vector<uint8_t> record = dis::capture::pcapng_packet_record(packet);
		buffer.insert(buffer.end(), record.begin(), record.end());
	}
	packet_count++;
	byte_count += bytes.size();
	if (buffer.size() >= FLUSH_BYTES) {
		flush();
	}
}
