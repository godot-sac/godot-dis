"""Writes addons/dis/data/sidc_map.json: DIS entity type -> MIL-STD-2525D symbol (approximate).

Usage: tools/.venv/Scripts/python.exe tools/sidc_map.py   (after tools/enums.py)
Check against milsymbol: node tools/check_sidc_map.mjs

DIS entity types describe platforms by kind / domain / category; 2525D symbols describe them by symbol set and
entity code. This table is this project's own judgement (MIT), not part of either standard. Each entry maps a
"kind:domain:category" key to [symbol set, entity code, modifier 1]. Keys are tried from the most specific
("1:2:20") to the least ("1:2"); DisEnums.entity_type_to_sidc() adds the standard identity from the force ID.
"""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "addons" / "dis" / "data" / "sidc_map.json"

# Symbol sets: 01 air, 05 space, 10 land unit, 15 land equipment, 30 sea surface, 35 sea subsurface.
LAND, AIR, SURFACE, SUBSURFACE, SPACE = "1:1", "1:2", "1:3", "1:4", "1:5"

MAP = {
    # Platform, land (category table uid 9) -> land equipment
    LAND: ["15", "000000", "00"],
    f"{LAND}:1": ["15", "120200", "00"],  # Tank
    f"{LAND}:2": ["15", "120101", "00"],  # Armored Fighting Vehicle
    f"{LAND}:3": ["15", "120100", "00"],  # Armored Utility Vehicle -> armoured vehicle
    f"{LAND}:4": ["15", "110900", "00"],  # Self-Propelled Artillery -> howitzer
    f"{LAND}:5": ["15", "110900", "00"],  # Towed Artillery -> howitzer
    f"{LAND}:6": ["15", "140100", "00"],  # Small Wheeled Utility Vehicle
    f"{LAND}:7": ["15", "140100", "00"],  # Large Wheeled Utility Vehicle
    f"{LAND}:8": ["15", "140800", "00"],  # Small Tracked Utility Vehicle -> utility vehicle, cross-country
    f"{LAND}:9": ["15", "140800", "00"],  # Large Tracked Utility Vehicle
    f"{LAND}:10": ["15", "111400", "00"],  # Mortar
    f"{LAND}:11": ["15", "130900", "00"],  # Mine Plow -> mine clearing equipment
    f"{LAND}:12": ["15", "130900", "00"],  # Mine Rake
    f"{LAND}:13": ["15", "130900", "00"],  # Mine Roller
    f"{LAND}:18": ["15", "130800", "00"],  # Engineer Equipment -> earthmover
    f"{LAND}:23": ["15", "220100", "00"],  # Warning System -> sensor
    f"{LAND}:24": ["15", "150100", "00"],  # Train - Engine
    f"{LAND}:25": ["15", "150200", "00"],  # Train - Car
    f"{LAND}:26": ["15", "150200", "00"],  # Train - Caboose
    f"{LAND}:27": ["15", "160100", "00"],  # Civilian Vehicle -> automobile
    f"{LAND}:28": ["15", "111100", "00"],  # Air Defense / Missile Defense -> AD missile launcher
    f"{LAND}:41": ["15", "130900", "00"],  # Mine Flail
    f"{LAND}:42": ["15", "110700", "00"],  # Fixed Barrel/Turret Gun -> direct fire gun
    f"{LAND}:43": ["15", "111000", "00"],  # Fixed Missile Launcher
    f"{LAND}:81": ["15", "160100", "00"],  # Car
    f"{LAND}:82": ["15", "140500", "00"],  # Bus
    f"{LAND}:83": ["15", "160200", "00"],  # Single Unit Cargo Truck -> open-bed truck
    f"{LAND}:84": ["15", "160400", "00"],  # Single Unit Utility/Emergency Truck
    f"{LAND}:85": ["15", "160200", "00"],  # Multiple Unit Cargo Truck
    f"{LAND}:86": ["15", "160400", "00"],  # Multiple Unit Utility/Emergency Truck
    f"{LAND}:87": ["15", "130800", "00"],  # Construction Specialty Vehicle -> earthmover
    f"{LAND}:89": ["15", "160600", "00"],  # Trailer
    f"{LAND}:92": ["15", "150100", "00"],  # Trains
    f"{LAND}:93": ["15", "160100", "00"],  # Utility/Emergency Car
    # Platform, air (uid 10) -> air
    AIR: ["01", "110000", "00"],
    f"{AIR}:1": ["01", "110104", "00"],  # Fighter/Air Defense
    f"{AIR}:2": ["01", "110102", "00"],  # Attack/Strike
    f"{AIR}:3": ["01", "110103", "00"],  # Bomber
    f"{AIR}:4": ["01", "110107", "00"],  # Cargo/Tanker -> cargo
    f"{AIR}:5": ["01", "110110", "00"],  # ASW/Patrol/Observation -> patrol
    f"{AIR}:6": ["01", "110108", "00"],  # Electronic Warfare -> jammer / ECM
    f"{AIR}:7": ["01", "110111", "00"],  # Reconnaissance
    f"{AIR}:8": ["01", "110116", "00"],  # Surveillance/C2 (AEW)
    f"{AIR}:9": ["01", "110125", "00"],  # Air-Sea Rescue -> search and rescue
    f"{AIR}:20": ["01", "110200", "01"],  # Attack Helicopter -> rotary wing + attack
    f"{AIR}:21": ["01", "110200", "07"],  # Utility Helicopter -> + utility
    f"{AIR}:22": ["01", "110200", "22"],  # ASW/Patrol Helicopter -> + antisubmarine warfare
    f"{AIR}:23": ["01", "110200", "03"],  # Cargo Helicopter -> + cargo
    f"{AIR}:24": ["01", "110200", "18"],  # Observation Helicopter -> + reconnaissance
    f"{AIR}:25": ["01", "110200", "27"],  # Special Operations Helicopter -> + SOF
    f"{AIR}:26": ["01", "110200", "19"],  # Training Helicopter -> + trainer
    f"{AIR}:40": ["01", "110112", "00"],  # Trainer
    f"{AIR}:50": ["01", "110300", "00"],  # Unmanned -> UAV
    f"{AIR}:57": ["01", "120100", "00"],  # Non-Combatant Commercial Aircraft -> civilian fixed wing
    **{f"{AIR}:{c}": ["01", "120100", "00"] for c in (80, 81, 83, 84, 85, 86, 87, 88)},  # civilian fixed wing
    **{f"{AIR}:{c}": ["01", "120200", "00"] for c in (90, 91, 92, 93)},  # civilian helicopter / autogyro
    f"{AIR}:100": ["01", "120400", "00"],  # Civilian balloon
    f"{AIR}:101": ["01", "120500", "00"],  # Civilian airship
    # Platform, surface (uid 11) -> sea surface
    SURFACE: ["30", "110000", "00"],
    f"{SURFACE}:1": ["30", "120100", "00"],  # Carrier
    f"{SURFACE}:2": ["30", "120202", "00"],  # Command Ship/Cruiser -> cruiser
    f"{SURFACE}:3": ["30", "120202", "00"],  # Guided Missile Cruiser
    f"{SURFACE}:4": ["30", "120203", "00"],  # Guided Missile Destroyer
    f"{SURFACE}:5": ["30", "120203", "00"],  # Destroyer
    f"{SURFACE}:6": ["30", "120204", "00"],  # Guided Missile Frigate
    f"{SURFACE}:7": ["30", "120501", "00"],  # Light/Patrol Craft
    f"{SURFACE}:8": ["30", "120405", "00"],  # Mine Countermeasure Ship/Craft
    f"{SURFACE}:9": ["30", "120306", "00"],  # Dock Landing Ship -> amphibious transport, dock
    f"{SURFACE}:10": ["30", "120307", "00"],  # Tank Landing Ship -> landing ship
    f"{SURFACE}:11": ["30", "120308", "00"],  # Landing Craft
    f"{SURFACE}:12": ["30", "120100", "00"],  # Light Carrier
    f"{SURFACE}:13": ["30", "120305", "00"],  # Cruiser/Helicopter Carrier -> amphibious assault, helicopter
    f"{SURFACE}:14": ["30", "120500", "00"],  # Hydrofoil -> patrol
    f"{SURFACE}:15": ["30", "140107", "00"],  # Air Cushion/Surface Effect -> hovercraft
    f"{SURFACE}:16": ["30", "130100", "00"],  # Auxiliary
    f"{SURFACE}:17": ["30", "130108", "00"],  # Auxiliary, Merchant Marine -> cargo ship
    f"{SURFACE}:18": ["30", "130200", "00"],  # Utility -> service craft
    f"{SURFACE}:19": ["30", "120700", "00"],  # Unmanned Surface Vehicle
    f"{SURFACE}:20": ["30", "120206", "00"],  # Littoral Combat Ship
    f"{SURFACE}:21": ["30", "130104", "00"],  # Surveillance Ship -> intelligence collector
    f"{SURFACE}:50": ["30", "120204", "00"],  # Frigate (including Corvette)
    f"{SURFACE}:51": ["30", "120201", "00"],  # Battleship
    f"{SURFACE}:52": ["30", "120202", "00"],  # Heavy Cruiser
    f"{SURFACE}:53": ["30", "130111", "00"],  # Destroyer Tender -> repair ship
    f"{SURFACE}:54": ["30", "120302", "00"],  # Amphibious Assault Ship
    f"{SURFACE}:55": ["30", "120300", "00"],  # Amphibious Cargo Ship -> amphibious warfare ship
    f"{SURFACE}:56": ["30", "120306", "00"],  # Amphibious Transport Dock
    f"{SURFACE}:57": ["30", "130101", "00"],  # Ammunition Ship
    f"{SURFACE}:58": ["30", "130102", "00"],  # Combat Stores Ship
    f"{SURFACE}:59": ["30", "130104", "00"],  # SURTASS -> intelligence collector
    f"{SURFACE}:60": ["30", "130109", "00"],  # Fast Combat Support Ship
    f"{SURFACE}:61": ["30", "130000", "00"],  # Non-Combatant Ship
    f"{SURFACE}:62": ["30", "140300", "00"],  # Coast Guard Cutters -> law enforcement vessel
    f"{SURFACE}:63": ["30", "140300", "00"],  # Coast Guard Boats
    f"{SURFACE}:64": ["30", "120500", "00"],  # Fast Attack Craft -> patrol
    f"{SURFACE}:65": ["30", "120801", "00"],  # Inflatable Boat -> RHIB
    f"{SURFACE}:80": ["30", "140110", "00"],  # Passenger Vessel
    f"{SURFACE}:81": ["30", "140101", "00"],  # Dry Cargo Ship
    f"{SURFACE}:82": ["30", "140109", "00"],  # Tanker
    f"{SURFACE}:83": ["30", "140100", "00"],  # Offshore Support Vessel -> merchant ship
    f"{SURFACE}:84": ["30", "140500", "00"],  # Private Motorboat
    f"{SURFACE}:85": ["30", "140400", "00"],  # Private Sailboat
    f"{SURFACE}:86": ["30", "140200", "00"],  # Fishing Vessel
    f"{SURFACE}:87": ["30", "140000", "00"],  # Other Vessels -> civilian
    f"{SURFACE}:88": ["30", "140000", "00"],  # Human-Powered Watercraft -> civilian
    # Platform, subsurface (uid 12) -> sea subsurface
    SUBSURFACE: ["35", "110100", "00"],  # submarine
    f"{SUBSURFACE}:8": ["35", "110400", "00"],  # UUV
    f"{SUBSURFACE}:16": ["35", "110200", "00"],  # Semi-Submersible Boats -> other submersible
    f"{SUBSURFACE}:80": ["35", "120100", "00"],  # Civilian Submarines -> civilian submersible
    f"{SUBSURFACE}:81": ["35", "120100", "00"],  # Civilian Submersibles
    f"{SUBSURFACE}:82": ["35", "120100", "00"],  # Civilian Semi-Submersible Boats
    # Platform, space (uid 13) -> space
    SPACE: ["05", "110000", "00"],
    f"{SPACE}:1": ["05", "110100", "00"],  # Manned Spacecraft -> space vehicle
    f"{SPACE}:2": ["05", "110100", "00"],  # Unmanned
    f"{SPACE}:3": ["05", "111900", "00"],  # Booster -> space launch vehicle
    f"{SPACE}:11": ["05", "110600", "00"],  # Satellite, unspecified -> satellite, general
    f"{SPACE}:12": ["05", "111100", "00"],  # Communication
    f"{SPACE}:13": ["05", "111400", "00"],  # Navigation
    f"{SPACE}:14": ["05", "110700", "00"],  # Science / experimental -> satellite
    f"{SPACE}:15": ["05", "110700", "00"],  # Inert
    f"{SPACE}:16": ["05", "111200", "00"],  # Earth Observation
    f"{SPACE}:17": ["05", "111500", "00"],  # Space Surveillance -> reconnaissance satellite
    f"{SPACE}:18": ["05", "110900", "00"],  # Astronomy
    f"{SPACE}:19": ["05", "111800", "00"],  # Meteorology -> weather satellite
    f"{SPACE}:20": ["05", "110700", "00"],  # Military -> satellite
    # Other kinds, coarse
    "2": ["01", "130000", "00"],  # Munition -> air track, weapon
    "3:1": ["10", "121100", "00"],  # Life form, land -> infantry
    "3": ["10", "000000", "00"],
}

# Fallback per domain for kinds without their own entry (e.g. sensors / emitters).
DOMAIN_FALLBACK = {"0": ["10", "000000", "00"], "1": ["15", "000000", "00"], "2": ["01", "000000", "00"],
                   "3": ["30", "000000", "00"], "4": ["35", "000000", "00"], "5": ["05", "000000", "00"]}


def main():
    enums = json.loads((ROOT / "addons" / "dis" / "data" / "enums.json").read_text(encoding="utf-8"))["tables"]
    tables = {"1": "platform_land_category", "2": "platform_air_category", "3": "platform_surface_category",
              "4": "platform_subsurface_category", "5": "platform_space_category"}
    names = {}
    for key in MAP:
        parts = key.split(":")
        if len(parts) == 3 and parts[0] == "1":
            name = enums[tables[parts[1]]].get(parts[2])
            if name is None:
                raise SystemExit(f"{key}: no such category in SISO-REF-010")
            names[key] = name
    payload = {
        "description": "DIS entity type (kind:domain:category) -> MIL-STD-2525D [symbol set, entity, modifier 1]. "
        "Approximate; generated by tools/sidc_map.py.",
        "map": MAP,
        "domain_fallback": DOMAIN_FALLBACK,
        "names": names,
    }
    OUT.write_text(json.dumps(payload, indent=1, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    print(f"{len(MAP)} entries -> {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
