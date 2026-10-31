#pragma once

#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>

// Decides which scene represents a remote entity. A remote entity manager looks up the SISO-REF-010 entity type
// (the 7 fields kind:domain:country:category:subcategory:specific:extra) of every received entity in a
// DisEntityTypeMap and instantiates the scene of the best matching DisEntityTypeMapping.

// One pattern -> scene pair. Pattern fields are non-negative integers or "*" (wildcard). Trailing fields may be
// left out and then count as wildcards: "1:2" matches every air platform.
class DisEntityTypeMapping : public godot::Resource {
	GDCLASS(DisEntityTypeMapping, godot::Resource)

public:
	static constexpr int FIELD_COUNT = 7;

private:
	godot::String entity_type;
	godot::Ref<godot::PackedScene> scene;
	int32_t fields[FIELD_COUNT] = { -1, -1, -1, -1, -1, -1, -1 }; // -1 = wildcard
	bool valid = false;

protected:
	static void _bind_methods();

public:
	void set_entity_type(const godot::String &value);
	godot::String get_entity_type() const { return entity_type; }
	void set_scene(const godot::Ref<godot::PackedScene> &value) { scene = value; }
	godot::Ref<godot::PackedScene> get_scene() const { return scene; }

	// Entity types shorter than 7 fields count the missing fields as 0. Invalid patterns never match.
	bool matches(const godot::PackedInt32Array &entity_type) const;
	// Number of non-wildcard fields, -1 if the pattern is invalid.
	int get_specificity() const;
};

class DisEntityTypeMap : public godot::Resource {
	GDCLASS(DisEntityTypeMap, godot::Resource)

	godot::Array mappings; // of DisEntityTypeMapping
	godot::Ref<godot::PackedScene> default_scene;

protected:
	static void _bind_methods();

public:
	void set_mappings(const godot::Array &value) { mappings = value; }
	godot::Array get_mappings() const { return mappings; }
	void set_default_scene(const godot::Ref<godot::PackedScene> &value) { default_scene = value; }
	godot::Ref<godot::PackedScene> get_default_scene() const { return default_scene; }

	// The matching mapping with the highest specificity. Ties go to the earliest entry. Null if nothing matches.
	godot::Ref<DisEntityTypeMapping> find_mapping(const godot::PackedInt32Array &entity_type) const;
	// Scene of find_mapping(), or default_scene if there is no match (or the match has no scene).
	godot::Ref<godot::PackedScene> find_scene(const godot::PackedInt32Array &entity_type) const;

	// The 7 pattern values (-1 for "*"), or an empty array if the pattern is invalid.
	static godot::PackedInt32Array parse_pattern(const godot::String &pattern);
};
