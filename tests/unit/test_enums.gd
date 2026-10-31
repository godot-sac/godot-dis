extends GutTest
## DisEnums: SISO-REF-010 names and the entity type -> MIL-STD-2525D SIDC bridge.
## Expected names are copied from SISO-REF-010 v37 (tools/.cache/SISO-REF-010-v37.xml).


func test_source_and_attribution() -> void:
	var source := DisEnums.get_source()
	assert_eq(source.version, "v37")
	assert_eq(source.attribution, "Reprinted with permission from SISO Inc.")
	assert_eq(source.sha256.length(), 64)


func test_enum_tables() -> void:
	assert_eq(DisEnums.get_enum_name("force_id", 1), "Friendly")
	assert_eq(DisEnums.get_enum_name("force_id", 2), "Opposing")
	assert_eq(DisEnums.get_enum_name("entity_kind", 1), "Platform")
	assert_eq(DisEnums.get_enum_name("platform_domain", 2), "Air")
	assert_eq(DisEnums.get_enum_name("country", 218), "Turkiye (Republic of) (TUR)")
	assert_eq(DisEnums.get_enum_name("country", 225), "United States of America (USA)")
	assert_eq(DisEnums.get_enum_name("dead_reckoning_algorithm", 4).is_empty(), false)
	assert_eq(DisEnums.get_enum_name("country", 65000), "", "unknown value")
	assert_eq(DisEnums.get_enum_name("no_such_table", 1), "", "unknown table")
	for table in DisEnums.TABLES:
		assert_ne(DisEnums.get_enum_name(table, 0) + DisEnums.get_enum_name(table, 1), "", table)


func test_entity_type_names_from_the_tree() -> void:
	assert_eq(DisEnums.get_entity_type_name(PackedInt32Array([1, 1, 218, 1, 2, 0, 0])), "Leopard 2A4TR")
	assert_eq(DisEnums.get_entity_type_name(PackedInt32Array([1, 1, 225, 1, 1, 3, 0])), "M1A2")
	assert_eq(DisEnums.get_entity_type_name(PackedInt32Array([1, 3, 218, 6, 1, 0, 0])), "Barbaros class")
	# Down to the extra field.
	assert_eq(DisEnums.get_entity_type_name(PackedInt32Array([1, 1, 13, 7, 1, 1, 1])),
			"RMMV HX40M Personnel Cargo Restraint and Segregation (PCRS)")


func test_entity_type_names_fall_back_to_generic_tables() -> void:
	# Unknown subcategory for this country: the category name is the best we know.
	assert_eq(DisEnums.get_entity_type_name(PackedInt32Array([1, 1, 218, 1, 99, 0, 0])), "Tank")
	# Country without entries: the generic platform category table.
	assert_eq(DisEnums.get_entity_type_name(PackedInt32Array([1, 2, 65000, 1, 0, 0, 0])), "Fighter/Air Defense")
	# Only kind and domain.
	assert_eq(DisEnums.get_entity_type_name(PackedInt32Array([1, 4, 0, 0, 0, 0, 0])), "Subsurface")
	assert_eq(DisEnums.get_entity_type_name(PackedInt32Array([1])), "Platform", "short arrays are padded")


func test_describe_entity_type() -> void:
	var d := DisEnums.describe_entity_type(PackedInt32Array([1, 1, 218, 1, 2, 0, 0]))
	assert_eq(d.kind.name, "Platform")
	assert_eq(d.domain.name, "Land")
	assert_eq(d.country.name, "Turkiye (Republic of) (TUR)")
	assert_eq(d.category.name, "Tank")
	assert_eq(d.subcategory.name, "Leopard 2A4TR")
	assert_eq(d.subcategory.value, 2)
	assert_eq(d.specific.name, "")
	assert_eq(d.name, "Leopard 2A4TR")


func test_force_to_standard_identity() -> void:
	var expected := {0: "1", 1: "3", 2: "6", 3: "4", 4: "3", 5: "6", 6: "4", 7: "3", 8: "6", 9: "4", 10: "1"}
	for force in expected:
		assert_eq(DisEnums.force_to_standard_identity(force), expected[force], "force %d" % force)


func test_sidc_for_platforms() -> void:
	# 20 digits: version 10, context 0, identity, symbol set, status, HQ, amplifier (2), entity (6), modifiers (2+2).
	var tank := DisEnums.entity_type_to_sidc(PackedInt32Array([1, 1, 218, 1, 2, 0, 0]), 1)
	assert_eq(tank, "10031500001202000000", "friendly tank")
	assert_eq(DisEnums.entity_type_to_sidc(PackedInt32Array([1, 2, 225, 1, 0, 0, 0]), 2), "10060100001101040000",
			"hostile fighter")
	assert_eq(DisEnums.entity_type_to_sidc(PackedInt32Array([1, 2, 225, 20, 0, 0, 0]), 3), "10040100001102000100",
			"neutral attack helicopter: rotary wing + modifier 1 attack")
	assert_eq(DisEnums.entity_type_to_sidc(PackedInt32Array([1, 3, 218, 6, 1, 0, 0]), 1), "10033000001202040000",
			"frigate")
	assert_eq(DisEnums.entity_type_to_sidc(PackedInt32Array([1, 4, 218, 5, 2, 0, 0]), 2), "10063500001101000000",
			"submarine")
	assert_eq(DisEnums.entity_type_to_sidc(PackedInt32Array([1, 1, 225, 1, 1, 3, 0]), 1, 1), "10031510001202000000",
			"planned status")


func test_sidc_fallbacks() -> void:
	# Land category without a mapping: land equipment frame only.
	assert_eq(DisEnums.entity_type_to_sidc(PackedInt32Array([1, 1, 0, 30, 0, 0, 0]), 0), "10011500000000000000")
	# Sensor/emitter kind in the air domain: air frame.
	assert_eq(DisEnums.entity_type_to_sidc(PackedInt32Array([9, 2, 0, 0, 0, 0, 0]), 2), "10060100000000000000")
	# Life form on land: infantry.
	assert_eq(DisEnums.entity_type_to_sidc(PackedInt32Array([3, 1, 225, 1, 0, 0, 0]), 1), "10031000001211000000")
	for i in 50:
		var sidc := DisEnums.entity_type_to_sidc(PackedInt32Array([randi() % 12, randi() % 7, randi() % 300,
				randi() % 120, 0, 0, 0]), randi() % 10)
		assert_eq(sidc.length(), 20, sidc)
		assert_true(sidc.is_valid_int(), sidc)
