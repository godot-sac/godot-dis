#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

// Phase 0 smoke test: proves that godot-cpp and open-dis-cpp build together and that
// Godot can call into the extension. Replaced by real PDU classes in Phase 1.
class DisProbe : public godot::RefCounted {
	GDCLASS(DisProbe, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	// Encodes a DIS 7 Entity State PDU with fixed, recognizable values (see dis_probe.cpp).
	static godot::PackedByteArray encode_test_entity_state();
	// Version string of the compiled library, e.g. "godot-dis 0.1.0 (open-dis-cpp 1.2.0, godot-cpp 10.0.0)".
	static godot::String get_version();
};
