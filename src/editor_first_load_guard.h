#pragma once

// Works around a Godot editor crash at shutdown (godotengine/godot#111048, #111645; fix pending in #123658).
//
// When a headless editor (`--import`, `--editor --quit`) discovers this extension during its first filesystem scan,
// the editor queues the generation of the extension class docs (`EditorHelp::_gen_extensions_docs`) and may run it
// after `EditorHelp::cleanup_doc()` has freed the doc data. That dereferences a null pointer as soon as it documents
// an exposed extension class, so a fresh project's first `--import` exits with a crash although it is complete.
//
// The guard is only installed when the extension is loaded while a headless editor is already running (the case
// above); an extension loaded at startup, a game or a windowed editor are untouched. It unregisters our classes
// when the scene tree's root window is freed: after the editor node and its doc data are gone, before the queued
// call runs. godot-cpp's own unregistration at deinitialization then has nothing left to do.

// Call at MODULE_INITIALIZATION_LEVEL_EDITOR initialization. Returns true when the guard is active (the extension was
// loaded by a headless editor that was already running).
bool install_editor_first_load_guard();
// Call at MODULE_INITIALIZATION_LEVEL_EDITOR deinitialization (the extension is unloaded before the root is freed).
void remove_editor_first_load_guard();
