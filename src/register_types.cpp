#include "register_types.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "dis_probe.h"
#include "class_reference.h"
#include "editor_first_load_guard.h"
#include "pdu/dis_entity_state_pdu.h"
#include "geo/dis_geo.h"
#include "entity/dis_entity.h"
#include "entity/dis_entity_manager.h"
#include "entity/dis_entity_type_map.h"
#include "geo/dis_geo_origin.h"
#include "management/dis_exercise_control.h"
#include "network/dis_network.h"
#include "pdu/dis_pdu.h"
#include "pdu/dis_collision_pdu.h"
#include "pdu/dis_datum_pdus.h"
#include "pdu/dis_emission_pdus.h"
#include "pdu/dis_entity_state_update_pdu.h"
#include "pdu/dis_variable_parameter.h"
#include "pdu/dis_pdu_base.h"
#include "pdu/dis_radio_pdus.h"
#include "pdu/dis_simulation_management_pdus.h"
#include "pdu/dis_warfare_pdus.h"
#include "radio/dis_radio_monitor.h"
#include "record/dis_player.h"
#include "record/dis_recorder.h"
#include "record/dis_recording.h"

using namespace godot;

void initialize_godot_dis(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		// Skipped when the guard is needed: the editor would crash at exit generating docs for it.
		if (!install_editor_first_load_guard()) {
			load_class_reference();
		}
		return;
	}
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(DisProbe);
	GDREGISTER_ABSTRACT_CLASS(DisPdu);
	GDREGISTER_ABSTRACT_CLASS(DisPduBase);
	GDREGISTER_CLASS(DisEntityStatePdu);
	GDREGISTER_ABSTRACT_CLASS(DisWarfarePdu);
	GDREGISTER_CLASS(DisFirePdu);
	GDREGISTER_CLASS(DisDetonationPdu);
	GDREGISTER_ABSTRACT_CLASS(DisSimulationManagementPdu);
	GDREGISTER_CLASS(DisRemoveEntityPdu);
	GDREGISTER_CLASS(DisStartResumePdu);
	GDREGISTER_CLASS(DisStopFreezePdu);
	GDREGISTER_CLASS(DisAcknowledgePdu);
	GDREGISTER_CLASS(DisCreateEntityPdu);
	GDREGISTER_ABSTRACT_CLASS(DisDatumPdu);
	GDREGISTER_CLASS(DisCommentPdu);
	GDREGISTER_CLASS(DisSetDataPdu);
	GDREGISTER_CLASS(DisDataPdu);
	GDREGISTER_CLASS(DisCollisionPdu);
	GDREGISTER_CLASS(DisTransmitterPdu);
	GDREGISTER_CLASS(DisSignalPdu);
	GDREGISTER_CLASS(DisReceiverPdu);
	GDREGISTER_CLASS(DisElectromagneticEmissionPdu);
	GDREGISTER_CLASS(DisDesignatorPdu);
	GDREGISTER_CLASS(DisIffPdu);
	GDREGISTER_CLASS(DisEntityStateUpdatePdu);
	GDREGISTER_ABSTRACT_CLASS(DisVariableParameter);
	GDREGISTER_CLASS(DisNetwork);
	GDREGISTER_ABSTRACT_CLASS(DisGeo);
	GDREGISTER_CLASS(DisGeoOrigin);
	GDREGISTER_CLASS(DisEntityTypeMapping);
	GDREGISTER_CLASS(DisEntityTypeMap);
	GDREGISTER_CLASS(DisEntity);
	GDREGISTER_CLASS(DisRemoteEntity);
	GDREGISTER_CLASS(DisEntityManager);
	GDREGISTER_CLASS(DisExerciseControl);
	GDREGISTER_CLASS(DisRecording);
	GDREGISTER_CLASS(DisRecorder);
	GDREGISTER_CLASS(DisPlayer);
	GDREGISTER_CLASS(DisRadioMonitor);
}

void uninitialize_godot_dis(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		remove_editor_first_load_guard();
		return;
	}
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
