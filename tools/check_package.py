"""Installs a package made by tools/package_addon.py into a new, empty Godot project and uses it there: the check
that a user who only has the zip gets a working addon (library found, classes registered, data folder found, PDUs
sent and received over UDP on localhost).

With --with, other add-on zips are installed next to it (e.g. godot-jsbsim's): the check then also loads them in
the same process. If godot-jsbsim is there, its bundled J3Cub flies for a few seconds; on Linux this is the
regression check for the libstdc++ symbol clash (see tools/check_exports.py).

Usage (from the repo root, any Python 3.9+):
    python tools/check_package.py build/package/godot-dis-0.1.0.zip --godot <path to the Godot executable>
        [--with godot-jsbsim-0.1.0.zip] [--port 3197]
Prints a PACKAGE_CHECK line; exit code 1 on failure.
"""
import argparse
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

PROJECT = """config_version=5

[application]
config/name="godot-dis package check"
"""

SMOKE = """extends SceneTree
## Two DisNetworks on localhost: a DisEntity publishes, a DisEntityManager shows it.

const PORT := %d
var _manager: DisEntityManager
var _deadline := 0
var _ok := true


func _initialize() -> void:
	for name in ["DisNetwork", "DisEntity", "DisEntityManager", "DisGeoOrigin", "DisRecorder", "DisPlayer",
			"DisExerciseControl", "DisRadioMonitor", "DisEntityStatePdu"]:
		if not ClassDB.class_exists(name):
			print("missing class ", name)
			_ok = false
	if not _ok:
		_finish()
		return
	var pdu := DisEntityStatePdu.new()
	pdu.marking = "CHECK"
	var decoded = DisPdu.decode(pdu.encode())
	_check(decoded is DisEntityStatePdu and decoded.marking == "CHECK", "encode / decode")
	var tank := DisEnums.get_entity_type_name(PackedInt32Array([1, 1, 225, 1, 1, 0, 0]))
	_check(tank != "", "enumeration data (%%s)" %% tank)
	_check(_jsbsim_flies(), "other add-ons in the same project")

	var origin := DisGeoOrigin.new()
	origin.set_origin_geodetic(39.93, 32.85, 900.0)
	root.add_child(origin)
	var sender := DisNetwork.new()
	sender.port = PORT
	sender.mode = DisNetwork.MODE_UNICAST
	sender.destination_address = "127.0.0.1"
	sender.destination_port = PORT + 1
	sender.site_id = 10
	root.add_child(sender)
	var receiver := DisNetwork.new()
	receiver.port = PORT + 1
	receiver.site_id = 20
	root.add_child(receiver)
	_manager = DisEntityManager.new()
	root.add_child(_manager)
	_manager.network_path = _manager.get_path_to(receiver)
	var entity := DisEntity.new()
	entity.marking = "UDP"
	entity.position = Vector3(30, 5, -40)
	root.add_child(entity)
	entity.network_path = entity.get_path_to(sender)
	_deadline = Time.get_ticks_msec() + 5000


func _process(_delta: float) -> bool:
	if _manager == null:
		return false
	if _manager.get_entity_count() > 0:
		var remote: DisRemoteEntity = _manager.get_entities()[0]
		_check(remote.marking == "UDP" and remote.global_position.distance_to(Vector3(30, 5, -40)) < 0.01,
			"entity over UDP (port %%d)" %% PORT)
		_finish()
	elif Time.get_ticks_msec() > _deadline:
		_check(false, "entity over UDP (port %%d): nothing received in 5 s" %% PORT)
		_finish()
	return false


func _jsbsim_flies() -> bool:
	if not ClassDB.class_exists("JSBSimAircraft"):
		return true  # godot-jsbsim not installed next to it
	var aircraft: Node3D = ClassDB.instantiate("JSBSimAircraft")
	aircraft.set("paused", true)
	root.add_child(aircraft)
	if not aircraft.call("start"):
		return false
	for i in 300:
		aircraft.call("advance", 1.0 / 60.0)
	var time: float = aircraft.call("get_fdm").call("get_sim_time")
	print("JSBSim J3Cub flew %%.2f s next to godot-dis" %% time)
	aircraft.free()
	return absf(time - 5.0) < 0.01


func _check(ok: bool, what: String) -> void:
	print(("ok      " if ok else "FAILED  ") + what)
	_ok = _ok and ok


func _finish() -> void:
	_manager = null
	print("PACKAGE_CHECK ", "ok" if _ok else "FAILED")
	quit(0 if _ok else 1)
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("package", type=Path)
    parser.add_argument("--godot", required=True)
    parser.add_argument("--with", dest="others", type=Path, action="append", default=[],
                        help="another add-on zip to install in the same project (repeatable)")
    parser.add_argument("--port", type=int, default=3197, help="UDP port of the check (and port + 1)")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="gdis-package-") as tmp:
        project = Path(tmp)
        (project / "project.godot").write_text(PROJECT, encoding="utf-8")
        (project / "smoke.gd").write_text(SMOKE % args.port, encoding="utf-8")
        for package in [args.package, *args.others]:
            with zipfile.ZipFile(package) as archive:
                archive.extractall(project)
        for library in (project / "addons").glob("*/bin/linux/*"):
            library.chmod(0o755)  # zipfile.extractall drops the permissions
        # The first import discovers the extension mid-session; Godot 4.7 crashes at the end of it unless
        # src/editor_first_load_guard.cpp works around godotengine/godot#111048, so its exit code is checked.
        first = subprocess.run([args.godot, "--headless", "--path", str(project), "--import"],
                               capture_output=True, text=True, encoding="utf-8", errors="replace")
        print(f"first import: exit {first.returncode}")
        if first.returncode != 0:
            print(first.stderr.strip()[-2000:], file=sys.stderr)
            print("PACKAGE_CHECK FAILED (the first import did not exit cleanly)")
            return 1
        run = subprocess.run([args.godot, "--headless", "--path", str(project), "-s", "res://smoke.gd"],
                             capture_output=True, text=True, encoding="utf-8", errors="replace")
        print("\n".join(line for line in run.stdout.splitlines()
                        if line.startswith(("ok ", "FAILED", "missing", "JSBSim", "PACKAGE_CHECK"))))
        errors = [line for line in run.stderr.splitlines() if "ERROR" in line or "SCRIPT ERROR" in line]
        if run.returncode != 0 or errors:
            print(run.stderr.strip()[-3000:], file=sys.stderr)
        if "PACKAGE_CHECK ok" not in run.stdout or run.returncode != 0:
            if "PACKAGE_CHECK" not in run.stdout:
                print(f"PACKAGE_CHECK FAILED (the script did not finish, exit {run.returncode})")
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
