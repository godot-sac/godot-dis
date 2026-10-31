#include "register_types.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "dis_probe.h"
#include "pdu/dis_entity_state_pdu.h"
#include "geo/dis_geo.h"
#include "entity/dis_entity.h"
#include "entity/dis_entity_manager.h"
#include "entity/dis_entity_type_map.h"
#include "geo/dis_geo_origin.h"
#include "network/dis_network.h"
#include "pdu/dis_pdu.h"

using namespace godot;

void initialize_godot_dis(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(DisProbe);
	GDREGISTER_ABSTRACT_CLASS(DisPdu);
	GDREGISTER_CLASS(DisEntityStatePdu);
	GDREGISTER_CLASS(DisNetwork);
	GDREGISTER_ABSTRACT_CLASS(DisGeo);
	GDREGISTER_CLASS(DisGeoOrigin);
	GDREGISTER_CLASS(DisEntityTypeMapping);
	GDREGISTER_CLASS(DisEntityTypeMap);
	GDREGISTER_CLASS(DisEntity);
	GDREGISTER_CLASS(DisRemoteEntity);
	GDREGISTER_CLASS(DisEntityManager);
}

void uninitialize_godot_dis(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}

extern "C" {
// Entry point named in addons/dis/dis.gdextension (entry_symbol).
GDExtensionBool GDE_EXPORT godot_dis_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
		const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
	init_obj.register_initializer(initialize_godot_dis);
	init_obj.register_terminator(uninitialize_godot_dis);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init_obj.init();
}
}
