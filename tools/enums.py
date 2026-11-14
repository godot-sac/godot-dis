"""Exports SISO-REF-010 enumerations to addons/dis/data/*.json for the addon (DisEnums).

Usage: tools/.venv/Scripts/python.exe tools/enums.py

Source: SISO-REF-010 v37 machine-readable XML ("Schema and API" material, royalty-free for all purposes with
the attribution "Reprinted with permission from SISO Inc."). It is fetched from a version-tagged, byte-exact mirror
of the official SISO distribution and checked against a pinned SHA-256. The downloaded file is cached in
tools/.cache/ (not committed). The document renditions (PDF/Word/HTML, noncommercial license) are not used.

Writes:
  addons/dis/data/enums.json         small tables: force ID, entity kind, domains, categories, country, DRM ...
  addons/dis/data/entity_types.json  the entity type tree (kind:domain:country -> category -> ... -> extra)
"""
import hashlib
import json
import pathlib
import urllib.request
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent.parent
CACHE = ROOT / "tools" / ".cache" / "SISO-REF-010-v37.xml"
OUT = ROOT / "addons" / "dis" / "data"

SOURCE = {
    "title": "SISO-REF-010-2026 Enumerations for Simulation Interoperability",
    "version": "v37",
    "date": "2026-05-25",
    "official": "http://www.sisostds.org/doclib/doclib.cfm?SISO_RID_1003262",
    "mirror": "https://raw.githubusercontent.com/ngjunsiang/siso-ref-010-xml/v37/SISO-REF-010.xml",
    "sha256": "eaafe0b868e5fe6b3497c915226f312ca9853c242fae11d2f6afcbfb53c8be8c",
    "attribution": "Reprinted with permission from SISO Inc.",
    "license": "SISO Schema and API license: royalty-free for all purposes with the attribution above. "
    "Not covered by this project's MIT license.",
}

# SISO table uid -> key in enums.json
TABLES = {
    4: "pdu_type",
    5: "protocol_family",
    6: "force_id",
    7: "entity_kind",
    8: "platform_domain",
    9: "platform_land_category",
    10: "platform_air_category",
    11: "platform_surface_category",
    12: "platform_subsurface_category",
    13: "platform_space_category",
    14: "munition_domain",
    15: "munition_category",
    29: "country",
    44: "dead_reckoning_algorithm",
    56: "variable_parameter_record_type",
    57: "attached_part",
    58: "articulated_part_metric",
    59: "articulated_part_class",
    60: "warhead",
    61: "fuse",
    62: "detonation_result",
    67: "stop_freeze_reason",
    69: "acknowledge_flag",
    70: "acknowledge_response",
}

NS = "{http://www.sisostds.org/schemas/SISO-REF-010/2.7}"


def load_xml() -> bytes:
    if not CACHE.exists():
        CACHE.parent.mkdir(parents=True, exist_ok=True)
        print(f"downloading {SOURCE['mirror']}")
        with urllib.request.urlopen(SOURCE["mirror"]) as response:
            CACHE.write_bytes(response.read())
    data = CACHE.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SOURCE["sha256"]:
        raise SystemExit(f"SHA-256 mismatch for {CACHE}: {digest} (expected {SOURCE['sha256']})")
    return data


def enum_tables(root) -> dict:
    tables = {}
    for element in root.iter(NS + "enum"):
        uid = int(element.get("uid"))
        if uid in TABLES:
            tables[TABLES[uid]] = {
                row.get("value"): row.get("description") for row in element.iter(NS + "enumrow")
            }
    missing = set(TABLES.values()) - set(tables)
    if missing:
        raise SystemExit(f"tables not found: {missing}")
    return tables


LEVELS = ["category", "subcategory", "specific", "extra"]


def entity_node(element, level: int) -> dict:
    """{"n": description, "c": {value: child}}; "c" only when there are children."""
    node = {"n": element.get("description")}
    if level < len(LEVELS):
        children = {}
        for child in element.findall(NS + LEVELS[level]):
            children[child.get("value")] = entity_node(child, level + 1)
        if children:
            node["c"] = children
    return node


def entity_types(root) -> dict:
    cet = next(e for e in root.iter(NS + "cet") if e.get("uid") == "30")
    result = {}
    for entity in cet.findall(NS + "entity"):
        key = f"{entity.get('kind')}:{entity.get('domain')}:{entity.get('country')}"
        categories = result.setdefault(key, {})
        for category in entity.findall(NS + "category"):
            categories[category.get("value")] = entity_node(category, 1)
    return result


def write(name: str, payload: dict) -> None:
    path = OUT / name
    text = json.dumps({"source": SOURCE, **payload}, ensure_ascii=False, separators=(",", ":"), sort_keys=False)
    path.write_text(text + "\n", encoding="utf-8", newline="\n")
    print(f"{path.relative_to(ROOT)}: {len(text) / 1024:.0f} KB")


def main():
    root = ET.fromstring(load_xml())
    OUT.mkdir(parents=True, exist_ok=True)
    tables = enum_tables(root)
    write("enums.json", {"tables": tables})
    types = entity_types(root)
    count = 0

    def walk(nodes):
        nonlocal count
        for node in nodes.values():
            count += 1
            walk(node.get("c", {}))

    for categories in types.values():
        walk(categories)
    write("entity_types.json", {"entity_types": types})
    print(f"{len(tables)} tables, {len(types)} kind:domain:country groups, {count} entity type entries")


if __name__ == "__main__":
    main()
