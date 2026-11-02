"""Checks that the Linux libraries export only the GDExtension entry point.

godot-cpp links libstdc++ statically on Linux. If its symbols are exported, another extension in the same project
that uses the shared libstdc++ (godot-jsbsim's libJSBSim) binds part of its std::locale / std::regex code to our copy
and crashes. SConstruct hides them with src/linux_exports.map; this check keeps it that way.

Usage (from the repo root, on Linux with binutils' nm):
    python3 tools/check_exports.py [library.so ...]
Without arguments it checks addons/dis/bin/linux/*.so. Exit code 1 if a library exports anything else.
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ALLOWED = {"godot_dis_library_init"}


def exported(library: Path) -> list[str]:
    """Names of the dynamic symbols the library defines (what other libraries can bind to)."""
    out = subprocess.run(["nm", "-D", "--defined-only", str(library)], check=True, capture_output=True,
                         text=True).stdout
    return [line.split()[-1] for line in out.splitlines() if line.strip()]


def main() -> int:
    libraries = [Path(a) for a in sys.argv[1:]] or sorted((ROOT / "addons" / "dis" / "bin" / "linux").glob("*.so"))
    if not libraries:
        print("no Linux library to check (addons/dis/bin/linux/*.so)", file=sys.stderr)
        return 1
    failed = False
    for library in libraries:
        names = exported(library)
        extra = sorted(set(names) - ALLOWED)
        missing = sorted(ALLOWED - set(names))
        status = "ok" if not extra and not missing else "FAILED"
        failed = failed or status != "ok"
        print(f"{library.name}: {len(names)} exported symbols, {status}")
        for name in extra[:20]:
            print(f"  unexpected: {name}")
        if len(extra) > 20:
            print(f"  ... and {len(extra) - 20} more")
        for name in missing:
            print(f"  missing: {name}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
