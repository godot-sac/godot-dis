#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include "pdu/dis_simulation_management_pdus.h"

class DisNetwork;

// Exercise control over DIS Simulation Management PDUs. One node plays both roles:
//
// Managed application (what every simulation needs): Start/Resume and Stop/Freeze addressed to this application
// change `state` (at the real world time they ask for, or at once) and emit started / stopped; a Remove Entity
// request for one of our DisEntity nodes emits remove_entity_requested and, with remove_entities, frees the node.
// A Create Entity request emits create_entity_requested; it is answered "able to comply" if something is connected
// to that signal (the game creates the entity), "unable to comply" otherwise.
// With auto_acknowledge each request is answered with an Acknowledge PDU. With pause_tree, stopping pauses the
// scene tree (the network and this node keep running so a Start/Resume can wake it up).
//
// Simulation manager: send_start_resume / send_stop_freeze / send_create_entity / send_remove_entity return a
// request ID. A request to
// one application is sent again every retry_interval until it is acknowledged (acknowledged signal) or
// max_retries is reached (request_failed). A request to all applications is sent once; every answer arrives as
// acknowledged, and request_failed fires if nobody answered.
class DisExerciseControl : public godot::Node {
	GDCLASS(DisExerciseControl, godot::Node)

public:
	enum State {
		STATE_RUNNING,
		STATE_STOPPED,
	};

private:
	struct Request {
		int64_t id = 0;
		int acknowledge_flag = 0;
		godot::Vector3i receiving_id;
		bool to_everyone = false;
		godot::Ref<DisSimulationManagementPdu> pdu;
		int sent = 0;
		int answers = 0;
		double next_time = 0.0; // next resend, or (to everyone) when to stop waiting
	};

	struct Pending {
		bool active = false;
		State state = STATE_RUNNING;
		double at = 0.0; // UNIX seconds
		godot::Ref<DisSimulationManagementPdu> pdu;
	};

	godot::NodePath network_path;
	bool auto_acknowledge = true;
	bool pause_tree = false;
	bool remove_entities = true;
	bool start_stopped = false;
	double retry_interval = 1.0;
	int max_retries = 3;

	State state = STATE_RUNNING;
	int frozen_behavior = 0;
	double simulation_time = 0.0;
	Pending pending;
	std::vector<Request> requests;
	std::deque<uint64_t> handled; // recent (originator, request ID) keys, to recognise resent requests
	int64_t next_request_id = 1;
	godot::ObjectID connected_network;

	DisNetwork *get_network();
	void connect_network();
	void disconnect_network();
	static double now();
	godot::Vector3i own_id(DisNetwork *network) const;
	bool already_handled(const godot::Ref<DisSimulationManagementPdu> &pdu);
	void acknowledge(DisNetwork *network, const godot::Ref<DisSimulationManagementPdu> &request, int flag, int response);
	void schedule(State target, double at, const godot::Ref<DisSimulationManagementPdu> &pdu);
	void apply(State target, const godot::Ref<DisSimulationManagementPdu> &pdu);
	int64_t send_request(const godot::Ref<DisSimulationManagementPdu> &pdu, int acknowledge_flag);
	void process_requests(double time);

	void on_start_resume(const godot::Ref<DisStartResumePdu> &pdu);
	void on_stop_freeze(const godot::Ref<DisStopFreezePdu> &pdu);
	void on_remove_entity(const godot::Ref<DisRemoveEntityPdu> &pdu);
	void on_create_entity(const godot::Ref<DisCreateEntityPdu> &pdu);
	void on_acknowledge(const godot::Ref<DisAcknowledgePdu> &pdu);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	DisExerciseControl();

	// Simulation manager role. receiving_id defaults to everyone (0xFFFF, 0xFFFF, 0xFFFF).
	// real_world_time: UNIX seconds when the receivers should act; 0 = at once.
	int64_t send_start_resume(const godot::Vector3i &receiving_id, double real_world_time, double simulation_time);
	int64_t send_stop_freeze(const godot::Vector3i &receiving_id, int reason, int frozen_behavior, double real_world_time);
	int64_t send_remove_entity(const godot::Vector3i &entity_id);
	// Asks the application `receiving_id` (site, application, 0) to create an entity.
	int64_t send_create_entity(const godot::Vector3i &receiving_id);
	// Requests still waiting for an answer: [{request_id, acknowledge_flag, receiving_id, sent, answers}].
	godot::Array get_pending_requests() const;

	// Managed application role: the same transitions by hand (no PDU involved).
	void start();
	void stop(int new_frozen_behavior);
	// Handles a received PDU as if it came from the network (normally connected to the DisNetwork signals).
	void handle_pdu(const godot::Ref<DisPduBase> &pdu);

	State get_state() const { return state; }
	bool is_running() const { return state == STATE_RUNNING; }
	int get_frozen_behavior() const { return frozen_behavior; }
	// Seconds of simulation time: set by Start/Resume, advancing while running (or stopped with
	// FROZEN_RUN_SIMULATION_CLOCK).
	double get_simulation_time() const { return simulation_time; }
	// UNIX seconds of a scheduled change, or 0 if none is waiting.
	double get_scheduled_time() const { return pending.active ? pending.at : 0.0; }

	void set_network_path(const godot::NodePath &value);
	godot::NodePath get_network_path() const { return network_path; }
	void set_auto_acknowledge(bool value) { auto_acknowledge = value; }
	bool get_auto_acknowledge() const { return auto_acknowledge; }
	void set_pause_tree(bool value) { pause_tree = value; }
	bool get_pause_tree() const { return pause_tree; }
	void set_remove_entities(bool value) { remove_entities = value; }
	bool get_remove_entities() const { return remove_entities; }
	void set_start_stopped(bool value) { start_stopped = value; }
	bool get_start_stopped() const { return start_stopped; }
	void set_retry_interval(double value) { retry_interval = value; }
	double get_retry_interval() const { return retry_interval; }
	void set_max_retries(int value) { max_retries = value; }
	int get_max_retries() const { return max_retries; }
};

VARIANT_ENUM_CAST(DisExerciseControl::State);
