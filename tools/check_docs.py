#!/usr/bin/env python3
"""Checks the editor class reference in doc_classes/ (built into the debug library, shown on F1).

Every class registered in src/ (GDREGISTER_*CLASS) must have doc_classes/<Class>.xml, and every brief description
and description in those files must be filled in. Run it after changing the API, once the XML is regenerated with
<godot> --headless --path . --doctool . --gdextension-docs

Usage: python tools/check_docs.py
"""
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def registered_classes():
    names = set()
    for path in (ROOT / "src").rglob("*.cpp"):
        names.update(re.findall(r"GDREGISTER_\w*CLASS\((\w+)\)", path.read_text(encoding="utf-8", errors="replace")))
    return names


def main():
    problems = []
    classes = registered_classes()
    docs = {p.stem: p for p in (ROOT / "doc_classes").glob("*.xml")}
    for name in sorted(classes - docs.keys()):
        problems.append(f"{name}: no doc_classes/{name}.xml")
    for name in sorted(docs.keys() - classes):
        problems.append(f"doc_classes/{name}.xml: no class {name} is registered in src/")
    for name, path in sorted(docs.items()):
        root = ET.parse(path).getroot()
        for tag in ("brief_description", "description"):
            node = root.find(tag)
            if node is None or not (node.text or "").strip():
                problems.append(f"{name}: empty {tag}")
        for group, item in (("methods", "method"), ("members", "member"), ("signals", "signal"),
                            ("constants", "constant")):
            for entry in root.iterfind(f"{group}/{item}"):
                text = entry.text if item in ("member", "constant") else getattr(entry.find("description"), "text", "")
                if not (text or "").strip():
                    problems.append(f"{name}: {item} {entry.get('name')} has no description")
    for line in problems:
        print(line)
    print(f"check_docs: {len(docs)} classes, {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
