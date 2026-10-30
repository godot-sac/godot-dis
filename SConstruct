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

library = env.SharedLibrary(
    "addons/dis/bin/{}/libgodot_dis{}{}".format(env["platform"], env["suffix"], env["SHLIBSUFFIX"]),
    source=sources + opendis_sources,
)
Default(library)
