#include "dis_probe.h"

#include <dis7/EntityStatePdu.h>
#include <dis7/utils/DataStream.h>

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void DisProbe::_bind_methods() {
	ClassDB::bind_static_method("DisProbe", D_METHOD("encode_test_entity_state"), &DisProbe::encode_test_entity_state);
	ClassDB::bind_static_method("DisProbe", D_METHOD("get_version"), &DisProbe::get_version);
}

PackedByteArray DisProbe::encode_test_entity_state() {
	DIS::EntityStatePdu pdu;
	pdu.setProtocolVersion(7);
	pdu.setExerciseID(1);

	// In DIS 7 the entity ID is a simulation address (site, application) plus an entity number.
	DIS::SimulationAddress address;
	address.setSite(1);
	address.setApplication(2);
	DIS::EntityID id;
	id.setSimulationAddress(address);
	id.setEntityNumber(3);
	pdu.setEntityID(id);
	pdu.setForceId(1); // Friendly

	DIS::EntityType type;
	type.setEntityKind(1); // Platform
	type.setDomain(1); // Land
	type.setCountry(225);
	type.setCategory(1);
	type.setSubcategory(2);
	type.setSpecific(3);
	type.setExtra(4);
	pdu.setEntityType(type);

	DIS::Vector3Double location;
	location.setX(4000000.5);
	location.setY(3000000.25);
	location.setZ(2000000.125);
	pdu.setEntityLocation(location);

	DIS::Vector3Float velocity;
	velocity.setX(1.5f);
	velocity.setY(2.5f);
	velocity.setZ(-3.5f);
	pdu.setEntityLinearVelocity(velocity);

	DIS::EulerAngles orientation;
	orientation.setPsi(0.5f);
	orientation.setTheta(-0.25f);
	orientation.setPhi(0.125f);
	pdu.setEntityOrientation(orientation);

	pdu.setEntityAppearance(0x01020304);

	DIS::EntityMarking marking;
	marking.setCharacterSet(1); // ASCII
	marking.setByStringCharacters("GODOT");
	pdu.setMarking(marking);

	// open-dis-cpp does not fill in the header length field by itself.
	pdu.setLength(static_cast<unsigned short>(pdu.getMarshalledSize()));

	// DIS is big-endian on the wire. DataStream owns a std::vector<char> buffer.
	DIS::DataStream stream(DIS::BIG);
	pdu.marshal(stream);

	PackedByteArray bytes;
	bytes.resize(static_cast<int64_t>(stream.size()));
	for (size_t i = 0; i < stream.size(); i++) {
		bytes[static_cast<int64_t>(i)] = static_cast<uint8_t>(stream[static_cast<unsigned int>(i)]);
	}
	return bytes;
}

String DisProbe::get_version() {
	return "godot-dis 0.0.1 (open-dis-cpp 1.2.0, godot-cpp 10.0.0)";
}
