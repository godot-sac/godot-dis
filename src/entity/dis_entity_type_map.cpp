#include "dis_entity_type_map.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

void DisEntityTypeMapping::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_entity_type", "value"), &DisEntityTypeMapping::set_entity_type);
	ClassDB::bind_method(D_METHOD("get_entity_type"), &DisEntityTypeMapping::get_entity_type);
	ClassDB::bind_method(D_METHOD("set_scene", "value"), &DisEntityTypeMapping::set_scene);
	ClassDB::bind_method(D_METHOD("get_scene"), &DisEntityTypeMapping::get_scene);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "entity_type"), "set_entity_type", "get_entity_type");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"),
			"set_scene", "get_scene");

	ClassDB::bind_method(D_METHOD("matches", "entity_type"), &DisEntityTypeMapping::matches);
	ClassDB::bind_method(D_METHOD("get_specificity"), &DisEntityTypeMapping::get_specificity);
}

void DisEntityTypeMapping::set_entity_type(const String &value) {
	entity_type = value;
	const PackedInt32Array parsed = DisEntityTypeMap::parse_pattern(value);
	valid = parsed.size() == FIELD_COUNT;
	for (int i = 0; i < FIELD_COUNT; i++) {
		fields[i] = valid ? parsed[i] : -1;
	}
	if (!valid) {
		UtilityFunctions::push_warning("DisEntityTypeMapping: invalid entity type pattern '", value, "'");
	}
}

bool DisEntityTypeMapping::matches(const PackedInt32Array &p_entity_type) const {
	if (!valid) {
		return false;
	}
	for (int i = 0; i < FIELD_COUNT; i++) {
		const int32_t actual = i < p_entity_type.size() ? p_entity_type[i] : 0;
		if (fields[i] != -1 && fields[i] != actual) {
			return false;
		}
	}
	return true;
}

int DisEntityTypeMapping::get_specificity() const {
	if (!valid) {
		return -1;
	}
	int count = 0;
	for (int i = 0; i < FIELD_COUNT; i++) {
		if (fields[i] != -1) {
			count++;
		}
	}
	return count;
}

void DisEntityTypeMap::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mappings", "value"), &DisEntityTypeMap::set_mappings);
	ClassDB::bind_method(D_METHOD("get_mappings"), &DisEntityTypeMap::get_mappings);
	ClassDB::bind_method(D_METHOD("set_default_scene", "value"), &DisEntityTypeMap::set_default_scene);
	ClassDB::bind_method(D_METHOD("get_default_scene"), &DisEntityTypeMap::get_default_scene);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "mappings", PROPERTY_HINT_TYPE_STRING,
						 String::num_int64(Variant::OBJECT) + "/" + String::num_int64(PROPERTY_HINT_RESOURCE_TYPE) +
								 ":DisEntityTypeMapping"),
			"set_mappings", "get_mappings");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "default_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"),
			"set_default_scene", "get_default_scene");

	ClassDB::bind_method(D_METHOD("find_mapping", "entity_type"), &DisEntityTypeMap::find_mapping);
	ClassDB::bind_method(D_METHOD("find_scene", "entity_type"), &DisEntityTypeMap::find_scene);
	ClassDB::bind_static_method("DisEntityTypeMap", D_METHOD("parse_pattern", "pattern"),
			&DisEntityTypeMap::parse_pattern);
}

PackedInt32Array DisEntityTypeMap::parse_pattern(const String &pattern) {
	const PackedStringArray parts = pattern.split(":", true);
	if (parts.size() < 1 || parts.size() > DisEntityTypeMapping::FIELD_COUNT) {
		return PackedInt32Array();
	}
	PackedInt32Array result;
	result.resize(DisEntityTypeMapping::FIELD_COUNT);
	result.fill(-1); // omitted trailing fields are wildcards
	for (int i = 0; i < parts.size(); i++) {
		const String field = parts[i].strip_edges();
		if (field == "*") {
			continue;
		}
		// Digits only: rejects empty, negative and non-numeric fields. 9 digits keeps it inside int32.
		if (field.is_empty() || field.length() > 9) {
			return PackedInt32Array();
		}
		for (int j = 0; j < field.length(); j++) {
			if (field[j] < '0' || field[j] > '9') {
				return PackedInt32Array();
			}
		}
		result[i] = (int32_t)field.to_int();
	}
	return result;
}

Ref<DisEntityTypeMapping> DisEntityTypeMap::find_mapping(const PackedInt32Array &entity_type) const {
	Ref<DisEntityTypeMapping> best;
	int best_specificity = -1;
	for (int i = 0; i < mappings.size(); i++) {
		// Ref conversion yields null for null entries and for objects of other classes.
		Ref<DisEntityTypeMapping> mapping = mappings[i];
		if (mapping.is_null() || !mapping->matches(entity_type)) {
			continue;
		}
		const int specificity = mapping->get_specificity();
		if (specificity > best_specificity) { // strictly greater: the earliest wins ties
			best = mapping;
			best_specificity = specificity;
		}
	}
	return best;
}

Ref<PackedScene> DisEntityTypeMap::find_scene(const PackedInt32Array &entity_type) const {
	const Ref<DisEntityTypeMapping> mapping = find_mapping(entity_type);
	if (mapping.is_valid() && mapping->get_scene().is_valid()) {
		return mapping->get_scene();
	}
	return default_scene;
}
