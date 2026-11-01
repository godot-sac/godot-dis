#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include "dis_pdu_base.h"

// Simulation Management family (protocol family 5): the PDUs a simulation manager uses to control an exercise.
// Byte layouts: docs/simulation-management.md.
//
// Every PDU of the family starts with the originating and receiving IDs (site, application, entity).
// 0xFFFF in a field means "all" (all sites / applications / entities); entity 0 addresses the application itself.
class DisSimulationManagementPdu : public DisPduBase {
	GDCLASS(DisSimulationManagementPdu, DisPduBase)

protected:
	godot::Vector3i originating_id;
	godot::Vector3i receiving_id;
	int64_t request_id = 0;

	static void _bind_methods();

public:
	static constexpr int ID_ALL = 0xFFFF;

	int get_protocol_family() const override { return 5; }

	// True if receiving_id names this application (or all applications at its site, or all sites).
	// The entity number is not checked: entity 0, 0xFFFF and a specific entity all reach the application.
	bool is_addressed_to(int site, int application) const;

	void set_originating_id(const godot::Vector3i &value) { originating_id = value; }
	godot::Vector3i get_originating_id() const { return originating_id; }
	void set_receiving_id(const godot::Vector3i &value) { receiving_id = value; }
	godot::Vector3i get_receiving_id() const { return receiving_id; }
	void set_request_id(int64_t value) { request_id = value; }
	int64_t get_request_id() const { return request_id; }
};

// Clock Time record helpers: an hour count plus the time past the hour in DIS timestamp units (2^31 per hour,
// shifted left by one, low bit = absolute time), as in the PDU header timestamp.
namespace dis_clock {
double to_seconds(int64_t hour, int64_t time_past_hour);
void from_seconds(double seconds, bool absolute, int64_t &hour, int64_t &time_past_hour);
} // namespace dis_clock

// Start/Resume PDU (type 13): asks the receiver to start or resume at real_world_time, with its simulation clock
// set to simulation_time. 44 bytes.
class DisStartResumePdu : public DisSimulationManagementPdu {
	GDCLASS(DisStartResumePdu, DisSimulationManagementPdu)

	int64_t real_world_time_hour = 0;
	int64_t real_world_time_past_hour = 0;
	int64_t simulation_time_hour = 0;
	int64_t simulation_time_past_hour = 0;

protected:
	static void _bind_methods();

public:
	static constexpr int SIZE = 44;

	godot::PackedByteArray encode() const override;
	static godot::Ref<DisStartResumePdu> decode(const godot::PackedByteArray &bytes);
	int get_pdu_type() const override { return 13; }
	int get_length() const override { return SIZE; }

	void set_real_world_time_hour(int64_t value) { real_world_time_hour = value; }
	int64_t get_real_world_time_hour() const { return real_world_time_hour; }
	void set_real_world_time_past_hour(int64_t value) { real_world_time_past_hour = value; }
	int64_t get_real_world_time_past_hour() const { return real_world_time_past_hour; }
	void set_simulation_time_hour(int64_t value) { simulation_time_hour = value; }
	int64_t get_simulation_time_hour() const { return simulation_time_hour; }
	void set_simulation_time_past_hour(int64_t value) { simulation_time_past_hour = value; }
	int64_t get_simulation_time_past_hour() const { return simulation_time_past_hour; }
	// Seconds since the UNIX epoch (UTC), from / to the two real world time fields (absolute time bit set).
	void set_real_world_time(double seconds);
	double get_real_world_time() const;
	// Seconds of simulation time, from / to the two simulation time fields (relative time).
	void set_simulation_time(double seconds);
	double get_simulation_time() const;
};

// Stop/Freeze PDU (type 14): asks the receiver to stop or freeze at real_world_time. 40 bytes.
class DisStopFreezePdu : public DisSimulationManagementPdu {
	GDCLASS(DisStopFreezePdu, DisSimulationManagementPdu)

	int64_t real_world_time_hour = 0;
	int64_t real_world_time_past_hour = 0;
	int reason = 0;
	int frozen_behavior = 0;
	int padding = 0;

protected:
	static void _bind_methods();

public:
	static constexpr int SIZE = 40;

	// SISO-REF-010 "Stop/Freeze-Reason" (UID 67).
	enum Reason {
		REASON_OTHER = 0,
		REASON_RECESS = 1,
		REASON_TERMINATION = 2,
		REASON_SYSTEM_FAILURE = 3,
		REASON_SECURITY_VIOLATION = 4,
		REASON_ENTITY_RECONSTITUTION = 5,
		REASON_STOP_FOR_RESET = 6,
		REASON_STOP_FOR_RESTART = 7,
		REASON_ABORT_TRAINING_RETURN_TO_TACTICAL_OPERATIONS = 8,
	};
	// SISO-REF-010 "Stop/Freeze-Frozen Behavior" bits (UID 68). 0 = stop completely.
	enum FrozenBehavior {
		FROZEN_RUN_SIMULATION_CLOCK = 1,
		FROZEN_TRANSMIT_UPDATES = 2,
		FROZEN_PROCESS_UPDATES = 4,
	};

	godot::PackedByteArray encode() const override;
	static godot::Ref<DisStopFreezePdu> decode(const godot::PackedByteArray &bytes);
	int get_pdu_type() const override { return 14; }
	int get_length() const override { return SIZE; }

	void set_real_world_time_hour(int64_t value) { real_world_time_hour = value; }
	int64_t get_real_world_time_hour() const { return real_world_time_hour; }
	void set_real_world_time_past_hour(int64_t value) { real_world_time_past_hour = value; }
	int64_t get_real_world_time_past_hour() const { return real_world_time_past_hour; }
	void set_real_world_time(double seconds);
	double get_real_world_time() const;
	void set_reason(int value) { reason = value; }
	int get_reason() const { return reason; }
	void set_frozen_behavior(int value) { frozen_behavior = value; }
	int get_frozen_behavior() const { return frozen_behavior; }
	void set_padding(int value) { padding = value; }
	int get_padding() const { return padding; }
};

// Acknowledge PDU (type 15): the answer to Create Entity, Remove Entity, Start/Resume and Stop/Freeze.
// request_id repeats the request's. 32 bytes.
class DisAcknowledgePdu : public DisSimulationManagementPdu {
	GDCLASS(DisAcknowledgePdu, DisSimulationManagementPdu)

	int acknowledge_flag = 0;
	int response_flag = 0;

protected:
	static void _bind_methods();

public:
	static constexpr int SIZE = 32;

	// SISO-REF-010 "Acknowledge-Acknowledge Flag" (UID 69): which request this answers.
	enum AcknowledgeFlag {
		ACKNOWLEDGE_CREATE_ENTITY = 1,
		ACKNOWLEDGE_REMOVE_ENTITY = 2,
		ACKNOWLEDGE_START_RESUME = 3,
		ACKNOWLEDGE_STOP_FREEZE = 4,
		ACKNOWLEDGE_TRANSFER_OWNERSHIP = 5,
	};
	// SISO-REF-010 "Acknowledge-Response Flag" (UID 70).
	enum ResponseFlag {
		RESPONSE_OTHER = 0,
		RESPONSE_ABLE_TO_COMPLY = 1,
		RESPONSE_UNABLE_TO_COMPLY = 2,
		RESPONSE_PENDING_OPERATOR_ACTION = 3,
		RESPONSE_REQUEST_FURTHER_INFORMATION = 4,
	};

	godot::PackedByteArray encode() const override;
	static godot::Ref<DisAcknowledgePdu> decode(const godot::PackedByteArray &bytes);
	int get_pdu_type() const override { return 15; }
	int get_length() const override { return SIZE; }

	void set_acknowledge_flag(int value) { acknowledge_flag = value; }
	int get_acknowledge_flag() const { return acknowledge_flag; }
	void set_response_flag(int value) { response_flag = value; }
	int get_response_flag() const { return response_flag; }
};

// Remove Entity PDU (type 12): asks the application that owns receiving_id to remove that entity. 28 bytes.
class DisRemoveEntityPdu : public DisSimulationManagementPdu {
	GDCLASS(DisRemoveEntityPdu, DisSimulationManagementPdu)

protected:
	static void _bind_methods();

public:
	static constexpr int SIZE = 28;

	godot::PackedByteArray encode() const override;
	static godot::Ref<DisRemoveEntityPdu> decode(const godot::PackedByteArray &bytes);
	int get_pdu_type() const override { return 12; }
	int get_length() const override { return SIZE; }
};

VARIANT_ENUM_CAST(DisStopFreezePdu::Reason);
VARIANT_ENUM_CAST(DisStopFreezePdu::FrozenBehavior);
VARIANT_ENUM_CAST(DisAcknowledgePdu::AcknowledgeFlag);
VARIANT_ENUM_CAST(DisAcknowledgePdu::ResponseFlag);
