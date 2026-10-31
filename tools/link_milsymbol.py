"""Links godot-milsymbol's addon into this project for the demo (symbols on the tactical map):

    addons/milsymbol  <- ../godot-milsymbol/addons/milsymbol

A directory junction on Windows (no admin rights needed), a symbolic link elsewhere; it is git-ignored.
Without it the demo still runs and draws simple markers instead of military symbols.

Usage (from the repo root, any Python 3): python tools/link_milsymbol.py [--source ../godot-milsymbol/addons/milsymbol]
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", type=Path, default=root.parent / "godot-milsymbol" / "addons" / "milsymbol")
    args = parser.parse_args()
    target = root / "addons" / "milsymbol"
    source = args.source.resolve()
    if target.exists() or target.is_symlink():
        print("addons/milsymbol already exists")
        return 0
    if not source.is_dir():
        print(f"missing {source}; clone godot-milsymbol next to this repository", file=sys.stderr)
        return 1
    if os.name == "nt":
        subprocess.run(["cmd", "/c", "mklink", "/J", str(target), str(source)], check=True, stdout=subprocess.DEVNULL)
    else:
        target.symlink_to(source, target_is_directory=True)
    print(f"linked addons/milsymbol -> {source}")
    print("Run once so Godot registers its classes: <godot> --headless --path . --import")
    return 0


if __name__ == "__main__":
    sys.exit(main())
