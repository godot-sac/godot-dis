#pragma once

// The editor class reference (F1 help) from doc_classes/*.xml. SConstruct compiles it into debug builds only
// (build/gen/class_reference.gen.cpp, defines CLASS_REFERENCE); release builds, which only games use, carry none.
//
// godot-cpp could register it by itself (env.GodotCPPDocData), but it would then always load it. When a headless
// editor discovers the extension during its first scan, the editor generates docs for the loaded XML after its own
// doc data is freed and crashes at exit (godotengine/godot#111048, see editor_first_load_guard.h). Loading by hand
// lets register_types.cpp skip it in exactly that case, where nobody can read the help anyway.

extern const int CLASS_REFERENCE_SIZE;
extern const int CLASS_REFERENCE_PACKED_SIZE;
extern const unsigned char CLASS_REFERENCE_PACKED[];

// Hands the class reference to the editor help. Call at MODULE_INITIALIZATION_LEVEL_EDITOR initialization, after the
// classes are registered. Does nothing in builds without the class reference.
void load_class_reference();
