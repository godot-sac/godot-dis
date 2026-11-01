"""Check that the first headless import of a new project with the addon exits cleanly.

Godot 4.7 crashes at the end of that import when an editor discovers a GDExtension during its first scan
(godotengine/godot#111048); src/editor_first_load_guard.cpp works around it. Each run copies addons/dis into an
empty project and runs `--import` once; the crash is timing dependent, so several runs are made.

Usage: tools/.venv/Scripts/python.exe tools/check_first_import.py --godot <godot executable> [--runs 3]
"""

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ADDON = ROOT / "addons" / "dis"
# Build by-products next to the libraries; not part of the addon.
IGNORE = shutil.ignore_patterns("*.lib", "*.exp", "*.pdb", "*.ilk")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--godot", required=True)
    parser.add_argument("--runs", type=int, default=3)
    args = parser.parse_args()

    failures = 0
    for i in range(args.runs):
        with tempfile.TemporaryDirectory(prefix="gdis-first-import-") as tmp:
            project = Path(tmp)
            (project / "project.godot").write_text("config_version=5\n", encoding="utf-8")
            shutil.copytree(ADDON, project / "addons" / "dis", ignore=IGNORE)
            run = subprocess.run([args.godot, "--headless", "--path", str(project), "--import"],
                                 capture_output=True, text=True, encoding="utf-8", errors="replace")
            print(f"FIRST_IMPORT run {i + 1}: exit {run.returncode}")
            if run.returncode != 0:
                failures += 1
                print(run.stderr.strip()[-2000:], file=sys.stderr)
    print("FIRST_IMPORT_CHECK", "ok" if failures == 0 else f"FAILED ({failures}/{args.runs})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
