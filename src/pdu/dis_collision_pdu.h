#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include "dis_pdu_base.h"

// Collision PDU (type 4, Entity Information family): the issuing entity collided with another entity (or with
// nothing known: colliding ID 0:0:0). 60 bytes. velocity is the issuing entity's (ECEF, m/s); location is the
// point of impact in the issuing entity's body coordinates (metres).
class DisCollisionPdu : public DisPduBase {
	GDCLASS(DisCollisionPdu, DisPduBase)

	godot::Vector3i issuing_entity_id;
	godot::Vector3i colliding_entity_id;
	godot::Vector3i event_id;
	int collision_type = 0;
	int padding = 0;
	godot::Vector3 velocity;
	double mass = 0.0; // kg
	godot::Vector3 location;

protected:
	static void _bind_methods();

public:
	static constexpr int SIZE = 60;

	// SISO-REF-010 "Collision Type" (UID 189), the two general values.
	enum CollisionType {
		COLLISION_INELASTIC = 0,
		COLLISION_ELASTIC = 1,
	};

	godot::PackedByteArray encode() const override;
	static godot::Ref<DisCollisionPdu> decode(const godot::PackedByteArray &bytes);
	int get_pdu_type() const override { return 4; }
	int get_protocol_family() const override { return 1; }
	int get_body_length() const override { return SIZE; }

	void set_issuing_entity_id(const godot::Vector3i &value) { issuing_entity_id = value; }
	godot::Vector3i get_issuing_entity_id() const { return issuing_entity_id; }
	void set_colliding_entity_id(const godot::Vector3i &value) { colliding_entity_id = value; }
	godot::Vector3i get_colliding_entity_id() const { return colliding_entity_id; }
	void set_event_id(const godot::Vector3i &value) { event_id = value; }
	godot::Vector3i get_event_id() const { return event_id; }
	void set_collision_type(int value) { collision_type = value; }
	int get_collision_type() const { return collision_type; }
	void set_padding(int value) { padding = value; }
	int get_padding() const { return padding; }
	void set_velocity(const godot::Vector3 &value) { velocity = value; }
	godot::Vector3 get_velocity() const { return velocity; }
	void set_mass(double value) { mass = value; }
	double get_mass() const { return mass; }
	void set_location(const godot::Vector3 &value) { location = value; }
	godot::Vector3 get_location() const { return location; }
};

VARIANT_ENUM_CAST(DisCollisionPdu::CollisionType);
