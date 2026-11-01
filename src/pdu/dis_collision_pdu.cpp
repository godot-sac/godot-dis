#include "dis_collision_pdu.h"

#include <dis7/CollisionPdu.h>

#include <godot_cpp/core/class_db.hpp>

#include "dis_pdu.h"
#include "pdu_bytes.h"
#include "pdu_convert.h"

using namespace godot;
using namespace pdu_convert;

void DisCollisionPdu::_bind_methods() {
	ClassDB::bind_static_method("DisCollisionPdu", D_METHOD("decode", "bytes"), &DisCollisionPdu::decode);
	ADD_GROUP("Collision", "");
	DIS_BIND_PROPERTY(DisCollisionPdu, VECTOR3I, issuing_entity_id);
	DIS_BIND_PROPERTY(DisCollisionPdu, VECTOR3I, colliding_entity_id);
	DIS_BIND_PROPERTY(DisCollisionPdu, VECTOR3I, event_id);
	DIS_BIND_PROPERTY(DisCollisionPdu, INT, collision_type);
	DIS_BIND_PROPERTY(DisCollisionPdu, INT, padding);
	DIS_BIND_PROPERTY(DisCollisionPdu, VECTOR3, velocity);
	DIS_BIND_PROPERTY(DisCollisionPdu, FLOAT, mass);
	DIS_BIND_PROPERTY(DisCollisionPdu, VECTOR3, location);
	ADD_GROUP("", "");

	BIND_ENUM_CONSTANT(COLLISION_INELASTIC);
	BIND_ENUM_CONSTANT(COLLISION_ELASTIC);
}

PackedByteArray DisCollisionPdu::encode() const {
	DIS::CollisionPdu pdu;
	write_header(pdu);
	pdu.setIssuingEntityID(to_dis_entity_id(issuing_entity_id));
	pdu.setCollidingEntityID(to_dis_entity_id(colliding_entity_id));
	pdu.setEventID(to_dis_event_id(event_id));
	pdu.setCollisionType(static_cast<unsigned char>(collision_type));
	pdu.setPad(static_cast<char>(padding));
	pdu.setVelocity(to_dis_vector(velocity));
	pdu.setMass(static_cast<float>(mass));
	pdu.setLocation(to_dis_vector(location));
	return marshal(pdu);
}

Ref<DisCollisionPdu> DisCollisionPdu::decode(const PackedByteArray &bytes) {
	if (!DisPdu::validate(bytes, DisPdu::PDU_TYPE_COLLISION, SIZE)) {
		return Ref<DisCollisionPdu>();
	}
	DIS::CollisionPdu pdu;
	DIS::DataStream stream = pdu_bytes::to_stream(bytes.slice(0, SIZE));
	pdu.unmarshal(stream);
	Ref<DisCollisionPdu> out;
	out.instantiate();
	out->read_header(pdu);
	out->issuing_entity_id = from_dis_entity_id(pdu.getIssuingEntityID());
	out->colliding_entity_id = from_dis_entity_id(pdu.getCollidingEntityID());
	out->event_id = from_dis_event_id(pdu.getEventID());
	out->collision_type = pdu.getCollisionType();
	out->padding = static_cast<uint8_t>(pdu.getPad());
	out->velocity = from_dis_vector(pdu.getVelocity());
	out->mass = pdu.getMass();
	out->location = from_dis_vector(pdu.getLocation());
	out->keep_trailing(bytes);
	return out;
}
