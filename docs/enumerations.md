# Enumerations and symbols

DIS sends numbers: force ID 2, entity type 1:1:218:1:2:0:0. `DisEnums` (GDScript, `addons/dis/dis_enums.gd`) turns
them into names from SISO-REF-010 and into MIL-STD-2525D symbol codes (SIDC) for
godot-milsymbol.

```gdscript
func _on_entity_appeared(entity: DisRemoteEntity) -> void:
	var name := DisEnums.get_entity_type_name(entity.entity_type)         # "Leopard 2A4TR"
	var side := DisEnums.get_enum_name("force_id", entity.force_id)       # "Friendly"
	var sidc := DisEnums.entity_type_to_sidc(entity.entity_type, entity.force_id)  # "10031500001202000000"
	$Symbol.sidc = sidc   # MilSymbolSprite3D from godot-milsymbol
```

## Names
| Method | |
|---|---|
| `get_enum_name(table, value)` | name in a table, `""` if unknown. Tables: `DisEnums.TABLES` (`force_id`, `entity_kind`, `platform_domain`, `platform_<land/air/surface/subsurface/space>_category`, `munition_domain`, `munition_category`, `country`, `dead_reckoning_algorithm`, `variable_parameter_record_type`, `articulated_part_class`, `articulated_part_metric`, `attached_part`) |
| `get_entity_type_name(entity_type)` | most specific known name: the country-specific entry (category → subcategory → specific → extra), else the generic category, else the domain or kind |
| `describe_entity_type(entity_type)` | every field as `{value, name}`, plus `name` |
| `get_source()` | version, date, SHA-256 and attribution of the data |

The data (`addons/dis/data/enums.json` 15 KB, `entity_types.json` 0.9 MB, 27 366 entries) is loaded on first use.

## Symbols
`entity_type_to_sidc(entity_type, force_id, status = 0)` builds a 20-digit 2525D SIDC:

- **Standard identity** from the force ID: Friendly (1, 4, 7) → friend, Opposing (2, 5, 8) → hostile,
  Neutral (3, 6, 9) → neutral, anything else → unknown.
- **Symbol set and entity** from `addons/dis/data/sidc_map.json`, looked up by `kind:domain:category`, then
  `kind:domain`, then `kind`, then a plain frame for the domain. Platforms map to land equipment (15), air (01),
  sea surface (30), sea subsurface (35) and space (05); helicopters are "rotary wing" with modifier 1
  (attack, utility, cargo ...).

The mapping is approximate (the two standards classify differently) and is this project's own table. Every entry is
checked against milsymbol with `node tools/check_sidc_map.mjs`, which also prints the milsymbol icon name next to
the DIS category for review.

All symbol sets used here (01, 05, 10, 15, 30, 35) are exported by godot-milsymbol (since its commit 6ac7523).

## Data source and license
`tools/enums.py` reads SISO-REF-010 v37 `SISO-REF-010.xml` (the machine-readable "Schema and API" material, free for
all uses with the attribution "Reprinted with permission from SISO Inc."), verified by SHA-256. The PDF/Word
renditions, which are licensed for noncommercial use only, are not used. The data is not covered by the MIT license;
see `addons/dis/data/NOTICE.txt` and `THIRD_PARTY_NOTICES.md`.

Regenerate (only for a new SISO-REF-010 version or a changed mapping):
```powershell
.\tools\.venv\Scripts\python.exe tools\enums.py
.\tools\.venv\Scripts\python.exe tools\sidc_map.py
node tools\check_sidc_map.mjs
```
