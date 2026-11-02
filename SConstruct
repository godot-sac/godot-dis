#!/usr/bin/env python
# Builds the godot-dis GDExtension: godot-cpp + open-dis-cpp (DIS 7) + src/.
# Usage (from the repo root): tools/.venv/Scripts/scons [target=template_debug|template_release] [-j16]
import os

env = SConscript("thirdparty/godot-cpp/SConstruct", {"api_version": "4.3"})

# open-dis-cpp includes its headers as <dis7/...>; src/compat provides the CMake-generated export header.
env.Append(CPPPATH=["src/", "src/compat/", "thirdparty/open-dis-cpp/src/"])

sources = Glob("src/*.cpp") + Glob("src/*/*.cpp")
opendis_env = env.Clone()
if opendis_env["platform"] == "windows" and not opendis_env.get("use_mingw"):
    # Third-party code: don't fail the build on its warnings.
    opendis_env.Append(CCFLAGS=["/W1"])
opendis_sources = [
    opendis_env.SharedObject(f)
    for f in Glob("thirdparty/open-dis-cpp/src/dis7/*.cpp") + Glob("thirdparty/open-dis-cpp/src/dis7/utils/*.cpp")
]

if env["platform"] == "windows":
    env.Append(LIBS=["ws2_32"])  # Winsock, used by src/network/udp_socket.cpp
if env["platform"] == "linux":
    # godot-cpp links libstdc++ statically on Linux. Without a version script the library exports ~4400 symbols,
    # most of them libstdc++'s, and another extension that uses the shared libstdc++ (godot-jsbsim's libJSBSim)
    # binds part of its std::locale / std::regex code to our copy and crashes. Export only the entry point;
    # tools/check_exports.py guards it.
    exports_map = File("src/linux_exports.map")
    env.Append(LINKFLAGS=["-Wl,--version-script=" + exports_map.abspath])
    env.Append(LINKFLAGS=["-Wl,--exclude-libs,ALL"])

library = env.SharedLibrary(
    "addons/dis/bin/{}/libgodot_dis{}{}".format(env["platform"], env["suffix"], env["SHLIBSUFFIX"]),
    source=sources + opendis_sources,
)
if env["platform"] == "linux":
    env.Depends(library, exports_map)
Default(library)

# C++ unit tests (doctest), without Godot: `scons tests` builds and runs build/tests/unit_tests.
# A separate environment, because godot-cpp's flags (no exceptions, DLL settings) don't suit a test program.
test_env = Environment(ENV=os.environ)
if env["platform"] == "windows" and not env.get("use_mingw"):
    test_env.Append(CXXFLAGS=["/std:c++17", "/EHsc", "/O2", "/W3", "/utf-8"])
else:
    test_env.Append(CXXFLAGS=["-std=c++17", "-O2", "-Wall"])
golden_dir = Dir("tests/golden").abspath.replace("\\", "/")
test_env.Append(CPPPATH=["src/", "thirdparty/doctest/"], CPPDEFINES=[("GOLDEN_DIR", '\\"{}\\"'.format(golden_dir))])
# Keep Godot from importing the object files in build/ (as it would try with .obj meshes).
os.makedirs("build", exist_ok=True)
open(os.path.join("build", ".gdignore"), "a").close()
test_objects = [
    test_env.Object("build/tests/" + os.path.splitext(os.path.basename(str(f)))[0], f)
    # Only the Godot-free parts of src/.
    for f in Glob("tests/cpp/*.cpp") + ["src/geo/geo_math.cpp", "src/entity/dead_reckoning.cpp", "src/record/capture_format.cpp"]
]
test_program = test_env.Program("build/tests/unit_tests", test_objects)
test_env.Alias("tests", test_program, test_program[0].abspath)
AlwaysBuild("tests")
