#include "record/dis_recording.h"

#include <algorithm>
#include <cstring>
#include <set>

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
using dis::capture::Packet;

namespace {
std::string to_std(const String &text) {
	return std::string(text.utf8().get_data());
}

PackedByteArray to_bytes(const std::vector<uint8_t> &data) {
	PackedByteArray out;
	out.resize(static_cast<int64_t>(data.size()));
	if (!data.empty()) {
		memcpy(out.ptrw(), data.data(), data.size());
	}
	return out;
}
} // namespace

void DisRecording::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load", "path", "port"), &DisRecording::load, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("save", "path", "format"), &DisRecording::save, DEFVAL(FORMAT_AUTO));
	ClassDB::bind_method(D_METHOD("clear"), &DisRecording::clear);
	ClassDB::bind_method(D_METHOD("add_packet", "bytes", "unix_time", "source_ip", "source_port", "destination_ip",
								 "destination_port"),
			&DisRecording::add_packet, DEFVAL(-1.0), DEFVAL("127.0.0.1"), DEFVAL(3000), DEFVAL("127.0.0.1"), DEFVAL(3000));
	ClassDB::bind_method(D_METHOD("get_packet_count"), &DisRecording::get_packet_count);
	ClassDB::bind_method(D_METHOD("get_start_time"), &DisRecording::get_start_time);
	ClassDB::bind_method(D_METHOD("get_duration"), &DisRecording::get_duration);
	ClassDB::bind_method(D_METHOD("get_time", "index"), &DisRecording::get_time);
	ClassDB::bind_method(D_METHOD("get_bytes", "index"), &DisRecording::get_bytes);
	ClassDB::bind_method(D_METHOD("get_packet", "index"), &DisRecording::get_packet);
	ClassDB::bind_method(D_METHOD("find_index", "time"), &DisRecording::find_index);
	ClassDB::bind_method(D_METHOD("get_summary"), &DisRecording::get_summary);
	ClassDB::bind_method(D_METHOD("get_last_error"), &DisRecording::get_last_error);
	ClassDB::bind_method(D_METHOD("get_read_statistics"), &DisRecording::get_read_statistics);

	BIND_ENUM_CONSTANT(FORMAT_AUTO);
	BIND_ENUM_CONSTANT(FORMAT_PCAPNG);
	BIND_ENUM_CONSTANT(FORMAT_PDULOG);
}

Error DisRecording::load(const String &path, int port) {
	clear();
	const PackedByteArray file = FileAccess::get_file_as_bytes(path);
	if (file.is_empty()) {
		last_error = vformat("cannot read %s (%s)", path, UtilityFunctions::error_string(FileAccess::get_open_error()));
		return FileAccess::get_open_error() != OK ? FileAccess::get_open_error() : ERR_FILE_CORRUPT;
	}
	const dis::capture::Format format = dis::capture::detect(file.ptr(), file.size(), to_std(path.get_file()));
	std::string error;
	const bool ok = dis::capture::read(file.ptr(), static_cast<size_t>(file.size()), format, packets, error, stats, port);
	// Captures are nearly always in time order; a stable sort fixes the few that are not.
	std::stable_sort(packets.begin(), packets.end(), [](const Packet &a, const Packet &b) { return a.time < b.time; });
	last_error = String::utf8(error.c_str());
	return ok ? OK : ERR_FILE_UNRECOGNIZED;
}

Error DisRecording::save(const String &path, Format format) const {
	dis::capture::Format file_format = dis::capture::Format::PCAPNG;
	if (format == FORMAT_PDULOG ||
			(format == FORMAT_AUTO && dis::capture::format_from_name(to_std(path)) == dis::capture::Format::PDULOG)) {
		file_format = dis::capture::Format::PDULOG;
	}
	const Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_null()) {
		return FileAccess::get_open_error();
	}
	if (file_format == dis::capture::Format::PDULOG) {
		const double start = get_start_time();
		std::string text = dis::capture::pdulog_header(start, "godot-dis");
		for (const Packet &packet : packets) {
			text += dis::capture::pdulog_line(packet, start);
		}
		file->store_buffer(to_bytes(std::vector<uint8_t>(text.begin(), text.end())));
	} else {
		file->store_buffer(to_bytes(dis::capture::pcapng_file_header("godot-dis")));
		for (const Packet &packet : packets) {
			file->store_buffer(to_bytes(dis::capture::pcapng_packet_record(packet)));
		}
	}
	return file->get_error();
}

void DisRecording::clear() {
	packets.clear();
	stats = dis::capture::ReadStats();
	last_error = "";
}

void DisRecording::add_packet(const PackedByteArray &bytes, double unix_time, const String &source_ip, int source_port,
		const String &destination_ip, int destination_port) {
	Packet packet;
	packet.time = unix_time >= 0.0 ? unix_time : Time::get_singleton()->get_unix_time_from_system();
	packet.data.assign(bytes.ptr(), bytes.ptr() + bytes.size());
	packet.source_ip = to_std(source_ip);
	packet.source_port = static_cast<uint16_t>(source_port);
	packet.destination_ip = to_std(destination_ip);
	packet.destination_port = static_cast<uint16_t>(destination_port);
	const auto position = std::upper_bound(packets.begin(), packets.end(), packet.time,
			[](double time, const Packet &p) { return time < p.time; });
	packets.insert(position, std::move(packet));
}

double DisRecording::get_start_time() const {
	return packets.empty() ? 0.0 : packets.front().time;
}

double DisRecording::get_duration() const {
	return packets.empty() ? 0.0 : packets.back().time - packets.front().time;
}

double DisRecording::get_time(int index) const {
	ERR_FAIL_INDEX_V(index, static_cast<int>(packets.size()), 0.0);
	return packets[index].time - packets.front().time;
}

PackedByteArray DisRecording::get_bytes(int index) const {
	ERR_FAIL_INDEX_V(index, static_cast<int>(packets.size()), PackedByteArray());
	return to_bytes(packets[index].data);
}

Dictionary DisRecording::get_packet(int index) const {
	ERR_FAIL_INDEX_V(index, static_cast<int>(packets.size()), Dictionary());
	const Packet &packet = packets[index];
	Dictionary out;
	out["time"] = packet.time - packets.front().time;
	out["unix_time"] = packet.time;
	out["bytes"] = to_bytes(packet.data);
	out["source_ip"] = String::utf8(packet.source_ip.c_str());
	out["source_port"] = packet.source_port;
	out["destination_ip"] = String::utf8(packet.destination_ip.c_str());
	out["destination_port"] = packet.destination_port;
	return out;
}

int DisRecording::find_index(double time) const {
	if (packets.empty()) {
		return 0;
	}
	const double target = packets.front().time + time;
	const auto position = std::lower_bound(packets.begin(), packets.end(), target,
			[](const Packet &p, double t) { return p.time < t; });
	return static_cast<int>(position - packets.begin());
}

Dictionary DisRecording::get_summary() const {
	Dictionary types;
	std::set<int> exercises;
	std::set<uint64_t> entities;
	std::set<uint32_t> sites;
	for (const Packet &packet : packets) {
		if (packet.data.size() < 12) {
			continue;
		}
		const int type = packet.data[2];
		types[type] = static_cast<int>(types.get(type, 0)) + 1;
		exercises.insert(packet.data[1]);
		const int family = packet.data[3];
		if (packet.data.size() >= 18 && (family == 1 || family == 2 || family == 4 || family == 5 || family == 10)) {
			const uint32_t site = (packet.data[12] << 8) | packet.data[13];
			const uint32_t application = (packet.data[14] << 8) | packet.data[15];
			sites.insert((site << 16) | application);
			if (type == 1) {
				const uint64_t number = (packet.data[16] << 8) | packet.data[17];
				entities.insert((static_cast<uint64_t>(site) << 32) | (static_cast<uint64_t>(application) << 16) | number);
			}
		}
	}
	Array exercise_list;
	for (const int id : exercises) {
		exercise_list.push_back(id);
	}
	Array site_list;
	for (const uint32_t key : sites) {
		site_list.push_back(vformat("%d:%d", key >> 16, key & 0xFFFF));
	}
	Dictionary out;
	out["packets"] = get_packet_count();
	out["duration"] = get_duration();
	out["start_time"] = get_start_time();
	out["pdu_types"] = types;
	out["exercises"] = exercise_list;
	out["entities"] = static_cast<int64_t>(entities.size());
	out["sites"] = site_list;
	return out;
}

Dictionary DisRecording::get_read_statistics() const {
	Dictionary out;
	out["frames"] = stats.frames;
	out["not_udp"] = stats.not_udp;
	out["fragments"] = stats.fragments;
	out["not_dis"] = stats.not_dis;
	out["filtered"] = stats.filtered;
	out["truncated"] = stats.truncated;
	return out;
}
