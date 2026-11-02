"""Packs addons/dis into a zip for users (the layout the Godot Asset Library expects: addons/dis/ at the root).

Checks that every requested platform has the debug and release libraries dis.gdextension names, leaves out linker
files (.lib, .exp, .pdb) and editor import files, and adds LICENSE and THIRD_PARTY_NOTICES.md. Entries are sorted
and carry a fixed date, so the same inputs give the same zip.

Usage (from the repo root, any Python 3.9+):
    python tools/package_addon.py [--version 0.1.0] [--platforms windows,linux,macos] [--out build/package]
Without --platforms it packs the platforms that are built and says which are missing.
"""
import argparse
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ADDON = ROOT / "addons" / "dis"
PLATFORMS = ["windows", "linux", "macos"]
SKIPPED_SUFFIXES = {".lib", ".exp", ".pdb", ".ilk", ".import"}
DATE = (2026, 1, 1, 0, 0, 0)  # zip entries need some date; a fixed one keeps the output reproducible


def required_files(platform: str) -> list[str]:
    """Library files dis.gdextension names for one platform (paths relative to the addon)."""
    text = (ADDON / "dis.gdextension").read_text(encoding="utf-8")
    paths = re.findall(r'^%s\.[^=]*=\s*"res://addons/dis/([^"]+)"' % platform, text, re.M)
    return sorted(set(paths))


def add(archive: zipfile.ZipFile, name: str, data: bytes, executable: bool = False) -> None:
    info = zipfile.ZipInfo(name, DATE)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = (0o100755 if executable else 0o100644) << 16  # Unix permissions for unzip on Linux / macOS
    info.create_system = 3
    archive.writestr(info, data, compresslevel=9)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--version", default="dev")
    parser.add_argument("--platforms", default="", help="comma-separated; all of them must be built")
    parser.add_argument("--out", type=Path, default=ROOT / "build" / "package")
    args = parser.parse_args()

    if args.platforms:
        platforms = [p.strip() for p in args.platforms.split(",") if p.strip()]
        unknown = set(platforms) - set(PLATFORMS)
        if unknown:
            print(f"unknown platforms: {', '.join(sorted(unknown))}", file=sys.stderr)
            return 1
    else:
        platforms = [p for p in PLATFORMS if (ADDON / "bin" / p).is_dir()]
    if not platforms:
        print("no platform is built (addons/dis/bin/<platform>/); see docs/building.md", file=sys.stderr)
        return 1

    required = {f for p in platforms for f in required_files(p)}
    missing = sorted(f for f in required if not (ADDON / f).exists())
    if missing:
        print("missing files:\n  " + "\n  ".join(missing), file=sys.stderr)
        return 1

    files: list[tuple[Path, str]] = []
    for path in sorted(ADDON.rglob("*")):
        rel = path.relative_to(ADDON)
        if path.is_dir() or path.suffix in SKIPPED_SUFFIXES:
            continue
        if rel.parts[0] == "bin":
            # Only the libraries dis.gdextension names (a macOS .framework / universal dylib may be a folder).
            if not any(rel.as_posix() == f or rel.as_posix().startswith(f + "/") for f in required):
                continue
        files.append((path, "addons/dis/" + rel.as_posix()))
    files.append((ROOT / "LICENSE", "addons/dis/LICENSE"))
    files.append((ROOT / "THIRD_PARTY_NOTICES.md", "addons/dis/THIRD_PARTY_NOTICES.md"))
    files.sort(key=lambda f: f[1])

    args.out.mkdir(parents=True, exist_ok=True)
    target = args.out / f"godot-dis-{args.version}.zip"
    with zipfile.ZipFile(target, "w") as archive:
        for path, name in files:
            add(archive, name, path.read_bytes(), executable=name.startswith("addons/dis/bin/"))
    absent = [p for p in PLATFORMS if p not in platforms]
    shown = target.relative_to(ROOT) if target.is_relative_to(ROOT) else target
    print(f"{shown.as_posix()}: {len(files)} files, {target.stat().st_size / 1e6:.1f} MB, "
          f"platforms {', '.join(platforms)}" + (f" (not included: {', '.join(absent)})" if absent else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
