extends GutTest
## DisEntityTypeMap / DisEntityTypeMapping: pattern parsing, matching and scene lookup.


func _mapping(pattern: String, scene: PackedScene = null) -> DisEntityTypeMapping:
	var m := DisEntityTypeMapping.new()
	m.entity_type = pattern
	m.scene = scene
	return m


func _map(mappings: Array, default_scene: PackedScene = null) -> DisEntityTypeMap:
	var map := DisEntityTypeMap.new()
	map.mappings = mappings
	map.default_scene = default_scene
	return map


func test_parse_full_pattern() -> void:
	assert_eq(DisEntityTypeMap.parse_pattern("1:2:225:1:1:3:0"), PackedInt32Array([1, 2, 225, 1, 1, 3, 0]))


func test_parse_short_pattern_pads_with_wildcards() -> void:
	assert_eq(DisEntityTypeMap.parse_pattern("1:2"), PackedInt32Array([1, 2, -1, -1, -1, -1, -1]))
	assert_eq(DisEntityTypeMap.parse_pattern("3"), PackedInt32Array([3, -1, -1, -1, -1, -1, -1]))


func test_parse_wildcards_and_whitespace() -> void:
	assert_eq(DisEntityTypeMap.parse_pattern("1:*:225"), PackedInt32Array([1, -1, 225, -1, -1, -1, -1]))
	assert_eq(DisEntityTypeMap.parse_pattern(" 1 : 2 :* "), PackedInt32Array([1, 2, -1, -1, -1, -1, -1]))
	assert_eq(DisEntityTypeMap.parse_pattern("*"), PackedInt32Array([-1, -1, -1, -1, -1, -1, -1]))


func test_parse_invalid_patterns() -> void:
	for pattern in ["", "1:2:3:4:5:6:7:8", "1::3", "1:2:", ":1", "-1:2", "a:2", "1.5:2", "1:+2", "1:x*"]:
		assert_eq(DisEntityTypeMap.parse_pattern(pattern).size(), 0, "'%s' should be invalid" % pattern)


func test_matches_full_and_wildcards() -> void:
	var et := PackedInt32Array([1, 2, 225, 1, 1, 3, 0])
	assert_true(_mapping("1:2:225:1:1:3:0").matches(et))
	assert_true(_mapping("1:2").matches(et))
	assert_true(_mapping("1:*:225").matches(et))
	assert_true(_mapping("*").matches(et))
	assert_false(_mapping("1:3").matches(et))
	assert_false(_mapping("1:2:225:1:1:3:1").matches(et))


func test_matches_short_entity_type_counts_missing_as_zero() -> void:
	var short := PackedInt32Array([1, 2])
	assert_true(_mapping("1:2").matches(short))
	assert_true(_mapping("1:2:0:0").matches(short))
	assert_false(_mapping("1:2:225").matches(short))
	assert_true(_mapping("*").matches(PackedInt32Array()))
	assert_false(_mapping("1").matches(PackedInt32Array()))


func test_invalid_pattern_never_matches() -> void:
	var m := _mapping("garbage")
	assert_false(m.matches(PackedInt32Array([1, 2, 3, 4, 5, 6, 7])))
	assert_false(m.matches(PackedInt32Array()))
	assert_eq(m.get_specificity(), -1)


func test_specificity() -> void:
	assert_eq(_mapping("*").get_specificity(), 0)
	assert_eq(_mapping("1:2").get_specificity(), 2)
	assert_eq(_mapping("1:*:225").get_specificity(), 2)
	assert_eq(_mapping("1:2:225:1:1:3:0").get_specificity(), 7)
	assert_eq(_mapping("1:2:*:*:*:*:*").get_specificity(), 2)


func test_most_specific_wins() -> void:
	var broad := PackedScene.new()
	var mid := PackedScene.new()
	var exact := PackedScene.new()
	# Order must not matter: the most specific entry is in the middle of the array.
	var map := _map([_mapping("1:2", broad), _mapping("1:2:225:1", exact), _mapping("1:2:225", mid)])
	assert_same(map.find_scene(PackedInt32Array([1, 2, 225, 1, 0, 0, 0])), exact)
	assert_same(map.find_scene(PackedInt32Array([1, 2, 225, 2, 0, 0, 0])), mid)
	assert_same(map.find_scene(PackedInt32Array([1, 2, 78, 1, 0, 0, 0])), broad)


func test_tie_goes_to_first() -> void:
	var first := PackedScene.new()
	var second := PackedScene.new()
	var map := _map([_mapping("1:2", first), _mapping("1:*:*", second), _mapping("*:2", second)])
	assert_same(map.find_scene(PackedInt32Array([1, 2, 5])), first)
	var m := map.find_mapping(PackedInt32Array([1, 2, 5]))
	assert_same(m, map.mappings[0])


func test_default_scene_fallback() -> void:
	var tank := PackedScene.new()
	var fallback := PackedScene.new()
	var map := _map([_mapping("1:1", tank)], fallback)
	assert_same(map.find_scene(PackedInt32Array([1, 1, 5])), tank)
	assert_same(map.find_scene(PackedInt32Array([1, 2, 5])), fallback)
	assert_null(map.find_mapping(PackedInt32Array([1, 2, 5])))


func test_no_match_and_no_default_is_null() -> void:
	var map := _map([_mapping("1:1", PackedScene.new())])
	assert_null(map.find_scene(PackedInt32Array([9, 9])))


func test_mapping_without_scene_falls_back_to_default() -> void:
	var fallback := PackedScene.new()
	var broad := PackedScene.new()
	# The more specific mapping wins even though it has no scene; the lookup then uses the default, not broad.
	var map := _map([_mapping("1:2", broad), _mapping("1:2:225")], fallback)
	assert_not_null(map.find_mapping(PackedInt32Array([1, 2, 225])))
	assert_same(map.find_scene(PackedInt32Array([1, 2, 225])), fallback)
	assert_same(map.find_scene(PackedInt32Array([1, 2, 1])), broad)


func test_null_and_foreign_entries_are_ignored() -> void:
	var scene := PackedScene.new()
	var map := DisEntityTypeMap.new()
	map.mappings = [null, Resource.new(), _mapping("1", scene), null]
	assert_same(map.find_scene(PackedInt32Array([1, 2, 3])), scene)
	assert_null(map.find_mapping(PackedInt32Array([2])))


func test_short_entity_type_lookup() -> void:
	var air := PackedScene.new()
	var generic := PackedScene.new()
	var map := _map([_mapping("1:2", air), _mapping("*", generic)])
	assert_same(map.find_scene(PackedInt32Array([1, 2])), air)
	assert_same(map.find_scene(PackedInt32Array([1])), generic)
	assert_same(map.find_scene(PackedInt32Array()), generic)
