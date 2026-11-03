# Making a release

1. Update `CHANGELOG.md`: move *Unreleased* to `## [x.y.z] - YYYY-MM-DD` (semver; 0.x while the API may change),
   and the version name in [compatibility](compatibility.md).
2. Check locally (see [building](building.md)): GUT, `scons tests`, then the package on Windows and Linux:
   ```sh
   python tools/package_addon.py --version x.y.z            # needs addons/dis/bin/windows and bin/linux
   python tools/check_package.py build/package/godot-dis-x.y.z.zip --godot <godot>
   python3 tools/check_exports.py                           # Linux only: the library exports one symbol
   ```
   With godot-jsbsim's release zip at hand, add `--with godot-jsbsim-x.y.z.zip` to the package check: both add-ons
   then load in one project and the J3Cub flies (on Linux this caught a crash, see
   [building](building.md#linux-symbol-visibility)).
3. Commit, tag `vx.y.z` and push the tag. CI builds all platforms, checks the exported symbols on Linux, packs
   `godot-dis-x.y.z.zip` (`addons/dis/` with the Windows, Linux and macOS libraries, data, icons, `LICENSE` and
   `THIRD_PARTY_NOTICES.md`), checks it in a new project and creates a **draft** GitHub release for the tag with the
   zip attached and the CHANGELOG section as notes (job `release`).
4. Read the draft through and publish it.

The zip name and layout are relied on by other projects (godot-sim-demo downloads `godot-dis-<version>.zip` from
the release of tag `v<version>` and expects `addons/dis/` at its root); keep them.

## Media
- `docs/images/demo.gif` (README animation, ~1 MB, 15 s at 2.5x speed) and `docs/images/social_preview.jpg`
  (1280×640; upload it in the repository's *Settings → General → Social preview*). To make them again, record the
  demo with a window (three terminals, from the repo root; port 3391 keeps it apart from a running demo):
  ```sh
  godot --headless --path . res://demo/main.tscn -- --side red --port 3391 --seconds 62 --auto-fire 6
  python tools/demo_traffic.py --port 3391 --seconds 62 --start 200
  godot --path . res://demo/main.tscn -- --side blue --port 3391 --seconds 50 --capture <frames> --auto-fire 5 --radio-at 9 --stop-at 32 --resume-at 37
  ```
  then `python tools/make_media.py <frames> --width 640` (needs Pillow; keeps the GIF under 1 MB).
  `--capture` saves 10 frames per second of real time
  (Godot's Movie Maker mode would slow the frame clock against the DIS wall clock).
- `icon.svg` (128×128) → `docs/images/icon.png` with `godot --headless --path . -s res://tools/render_icons.gd`:
  the Asset Library icon. The editor icons of the nodes are in `addons/dis/icons/` and ship with the addon.
- `docs/images/getting_started.png`: two copies of the [getting-started](getting-started.md) project.

## Godot Asset Library
The Asset Library installs an asset by downloading the repository archive of one commit. The compiled libraries
are not in git, so that archive alone does not work. Options:
- a `release` branch (or a separate repository) where CI commits `addons/dis/` with the libraries, and the
  Asset Library entry points to it;
- the newer Godot Asset Store, which takes an uploaded zip (the release zip as it is).

Entry text: name "godot-dis", category *Scripts* (or *Misc*), licence MIT (SISO-REF-010 data under SISO's terms,
noted in the description), Godot 4.3+, the README's first paragraph as the description, `docs/images/demo_blue.png`
as the preview, `docs/images/icon.png` as the icon.
