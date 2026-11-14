# C++ notes

These notes explain the C++ features and build details used in this addon, in the order they came up. Each concept is
added when it first appears in the code. Examples come from files in this repository.

## Build chain (Phase 0)

**What does building do?** Each `.cpp` file is *compiled* separately into an object file (`.obj`). Then the *linker*
combines them into a single library: `.dll` on Windows, `.so` on Linux, `.dylib` on macOS. Godot loads this library
at run time.

**Tools:**
- **MSVC (`cl.exe`)**: Visual Studio's C++ compiler. The most compatible option with Godot on Windows.
- **SCons**: a build tool written in Python. Godot and godot-cpp use it. The `SConstruct` file is a Python script
  that says which files to compile and with which settings. `scons -j16` compiles 16 files in parallel.
- **godot-cpp**: a library that wraps Godot's C API (GDExtension) in C++ classes. Godot types such as `Node`,
  `RefCounted` and `PackedByteArray` come from here. It is built once as a static library (`.lib`) and then embedded
  in the addon's DLL. The first build takes about 1 minute; later builds compile only the changed files.
- **git submodule**: adds another git repository as a subfolder, pinned to a specific commit.
  `thirdparty/godot-cpp` and `thirdparty/open-dis-cpp` are submodules. Clone with `git clone --recursive`.

**`api_version=4.3`**: tells godot-cpp to target the Godot 4.3 API. The same DLL then works in Godot 4.3, 4.4, ...,
4.7 (an addon that targets an older API runs in a newer Godot, not the other way around).

**`template_debug` / `template_release`**: the Godot editor and debug exports load the `template_debug` DLL; release
exports load the `template_release` DLL. Which file is which is declared in `addons/dis/dis.gdextension`.

## File structure

**Header (`.h`) and source (`.cpp`)**: in C++ a class's *declaration* (which functions it has) goes in the header
file and its *definition* (the function bodies) goes in the source file. Other files only `#include` the header.
Example: `src/dis_probe.h` and `src/dis_probe.cpp`.

**`#include <...>` and `#include "..."`**: the compiler copies the contents of the file in place. `<...>` searches
the include paths (`CPPPATH`); `"..."` searches the same folder first. open-dis-cpp headers are included as
`<dis7/EntityStatePdu.h>`, so `thirdparty/open-dis-cpp/src/` is added to the search path in `SConstruct`.

**`#pragma once`**: if the same header is included twice in one translation unit, the second include is ignored.

**Macro (`#define`)**: text substitution done before compilation. open-dis-cpp classes start with the
`OPENDIS7_EXPORT` macro (for exporting symbols from a DLL). Because open-dis-cpp is compiled *into* the addon's own
DLL, `src/compat/dis7/opendis7_export.h` defines this macro as empty.

## Language concepts

**Namespace (`DIS::EntityStatePdu`, `godot::PackedByteArray`)**: a scope that prevents name collisions.
`using namespace godot;` avoids writing the `godot::` prefix in that file (not done in header files).

**Class and inheritance**: `class DisProbe : public godot::RefCounted` means `DisProbe` inherits everything from
`RefCounted`. Same idea as `extends RefCounted` in GDScript.

**`static` function**: called without creating an object: `DisProbe.encode_test_entity_state()`.

**Reference (`const EntityID &pX`)**: passes the object itself rather than a copy (no copying, fast).
`const` is a promise that the function will not modify it.

**`static_cast<T>(x)`**: an explicit type conversion checked at compile time. Example: `size_t` (64-bit, unsigned)
to `int64_t` (the index type of Godot arrays). Preferred over C's `(int)x` because the intent is explicit and it is
safer.

**Stack objects**: `DIS::EntityStatePdu pdu;` is destroyed automatically when the function ends; no `new`/`delete`.
To keep memory management simple, the code is written this way wherever possible.

## GDExtension specifics

**`GDCLASS(DisProbe, godot::RefCounted)`**: a macro that generates the code needed to register with Godot's class
system.

**`_bind_methods()`**: declares which C++ functions are visible from GDScript.
`ClassDB::bind_static_method("DisProbe", D_METHOD("get_version"), &DisProbe::get_version)` makes
`DisProbe.get_version()` callable from GDScript. `&DisProbe::get_version` is the address of the function.

**`register_types.cpp` and the entry point**: when Godot loads the DLL, it calls the function named by
`entry_symbol` in the `.gdextension` file (`godot_dis_library_init`). This function registers the initialization and
shutdown functions; during initialization, `GDREGISTER_CLASS(DisProbe)` makes the classes known to Godot.

**`extern "C"`**: the C++ compiler "decorates" function names (name mangling). Godot looks up the entry point by its
plain C name, so that function is inside an `extern "C"` block.

## DIS specifics

**Endianness (byte order)**: DIS uses *big-endian* on the wire (most significant byte first); x86 processors are
*little-endian*. open-dis-cpp's `DataStream(DIS::BIG)` converts when writing. This is why the number 144 appears as
`00 90` in the packet, and why `decode_u16` (which reads little-endian) returns `0x9000` in the GUT test.

## Phase 1: PDU classes

**Global variables and DLL load order (a bug encountered earlier).** A global `godot::String` such as
`thread_local String last_error;` is constructed as soon as the DLL is loaded, that is, *before* Godot has
initialized godot-cpp. Constructing a `godot::String` calls into Godot's API, so loading crashed with "DLL
initialization routine failed". Rule: do not use Godot types (`String`, `Array`, `Ref`, ...) in global/static
variables; use plain C++ types such as `std::string`, or create the value on first use. (`src/pdu/dis_pdu.cpp`)

**`thread_local`**: a variable with a separate copy per thread. Used so that the network receive thread (Phase 2)
and the main thread do not overwrite each other's error messages.

**Anonymous namespace (`namespace { ... }`)**: the functions inside are visible only in that `.cpp` file (like a
`static` function in C). Helper functions (`read_u16_be`, `to_dis`, ...) are kept this way.

**Missing type error (`use of undefined type 'godot::Ref<...>'`)**: if a header only *forward-declares* a class
(says "such a class exists" without showing its contents), objects of that class cannot be created. The fix is to
include the header with the full definition (`#include <godot_cpp/classes/ref.hpp>`).

**`Ref<T>`**: Godot's reference-counting smart pointer. Objects derived from `RefCounted` are held with `Ref`; the
object is deleted when the last `Ref` goes away. `out.instantiate()` creates a new object; `Ref<T>()` is an empty
(null) reference.

**`constexpr`**: a constant known at compile time (`static constexpr int BASE_SIZE = 144;`). Safer than `#define`
because it has a type and follows namespace rules.

**Reducing repetition with a macro**: `DIS_PROPERTY(VECTOR3I, entity_id)` generates the same three lines for every
property (bind the setter, bind the getter, `ADD_PROPERTY`). `#name` turns a macro parameter into a string, and
`set_##name` joins two pieces into one identifier. Macros make debugging harder; they are used only for such
mechanical repetition and only inside one file (with `#undef`).

**`reinterpret_cast`**: treats a pointer as a completely different type (`const uint8_t*` to `const char*`). It does
not change any bits; it only tells the compiler "read this memory this way". Used only in deliberate places such as
byte buffers.

**`ERR_FAIL_COND_MSG(condition, message)`**: a godot-cpp macro: if the condition is true, it prints an error to
Godot and returns from the function. This is Godot-style error handling instead of C++ exceptions (godot-cpp is
built with exceptions disabled).

**Untrusted input**: never trust a byte array that comes from the network. open-dis-cpp uses `std::vector::at()`
when reading; because exceptions are disabled, an out-of-range read *terminates* the program. Therefore
`DisPdu::validate()` checks the lengths before handing data to open-dis-cpp, and the tests try this with 2000
corrupt packets.

## Phase 2: Network node

**A C++ class derived from `Node` and `_notification`**: GDScript's `_ready()` / `_process()` are caught in C++ by a
single `_notification(int p_what)` function with a `switch` (`NOTIFICATION_READY`, `NOTIFICATION_PROCESS`,
`NOTIFICATION_EXIT_TREE`). `set_process(true)` is required for `_process` notifications to arrive.
`Engine::get_singleton()->is_editor_hint()` prevents opening sockets in the editor. (`src/network/dis_network.cpp`)

**Signals**: `ADD_SIGNAL(MethodInfo("pdu_received", PropertyInfo(...), ...))` defines the signal and
`emit_signal("pdu_received", pdu, ip, port)` emits it. In GDScript it is listened to with
`network.pdu_received.connect(...)`.

**Using Godot classes from C++**: godot-cpp also exposes Godot's own classes (`PacketPeerUDP`, `IP`) to C++. Godot's
networking was used instead of writing network code from scratch: broadcast/multicast is ready on every platform.

**Type conversion with `Ref`**: `const Ref<DisEntityStatePdu> entity_state = pdu;` is valid if `pdu` really has that
type, otherwise it is empty (null). This is the C++ equivalent of GDScript's `if pdu is DisEntityStatePdu` check.

**Dynamic calls with `Variant`**: `pdu->call("encode")` and `pdu->get("exercise_id")` call a method or read a
property by name on a Godot object whose type is not known at compile time (like GDScript). `send_pdu` works this
way so that it works with all future PDU classes.

**Range-based `for`**: `for (const String &name : names)` yields each element of the array in order (like
`for name in names` in GDScript). `const &` avoids copying.

**Variadic macro arguments (`__VA_ARGS__`) and MSVC**: removing the comma on an empty argument with
`##__VA_ARGS__` is a GCC/Clang extension; MSVC's old preprocessor does not do it ("syntax error: ')'"). The
solution is two separate macros (`DIS_PROPERTY` and `DIS_PROPERTY_HINT`). When writing portable code, keep compiler
differences in mind.

## Phase 3: Coordinates

**A Godot-independent C++ core**: `src/geo/geo_math.{h,cpp}` includes no Godot header; it uses only the standard
library (`<cmath>`). The same file is therefore compiled both into the addon and into a separate test program
(`build/tests/test_geo.exe`). The classes exposed to Godot (`DisGeo`, `DisGeoOrigin`) are a thin layer on top of
this core.

**`namespace`**: `namespace dis::geo { ... }` groups names; from outside they are called as
`dis::geo::geodetic_to_ecef(...)`. This avoids a clash with the Godot method of the same name
(`DisGeo::geodetic_to_ecef`). `using namespace dis::geo;` allows using the names in a file without the prefix.

**`struct` and operator overloading**: `struct Vec3 { double x, y, z; Vec3 operator+(const Vec3 &o) const; }`
makes `a + b` possible. `struct` and `class` are the same thing; the only difference is that the default access is
`public`. `Vec3{1, 2, 3}` (curly braces) fills the fields in order.

**`constexpr`**: `constexpr double WGS84_A = 6378137.0;` is a constant known at compile time. Unlike `#define`, it
has a type and follows namespace rules.

**`inline` header functions**: small functions with their body in a header (`enu_to_godot`) must be `inline`;
otherwise every `.cpp` that includes the header produces its own copy and the linker reports a "defined twice"
error.

**A member and a method cannot have the same name**: `LocalFrame` had both a `Mat3 enu_to_ecef;` member and a
`Vec3 enu_to_ecef(...)` method; C++ rejects this. The member was renamed `enu_axes`. (The same rule applies in
GDScript.)

**Binding static methods**: `ClassDB::bind_static_method("DisGeo", D_METHOD(...), &DisGeo::geodetic_to_ecef)`
makes `DisGeo.geodetic_to_ecef(...)` callable from GDScript without creating an object. The class is registered
with `GDREGISTER_ABSTRACT_CLASS`, because creating an instance is meaningless.

**C++ unit tests with doctest**: `thirdparty/doctest/doctest.h` is a single-header test library. Tests are written
as `TEST_CASE("name") { CHECK(a == b); }`; `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` generates `main()`. `scons tests`
builds and runs them with a separate SCons environment (exceptions enabled, no Godot). Thousands of checks run in
seconds without opening Godot.

**`double` vs `float` (why it matters)**: `float` (32-bit) holds about 7 significant digits. An ECEF coordinate is
about 6,378,137 m; in 32 bits the smallest step is 0.5 m. `double` (64-bit) holds about 16 digits, which gives
nanometre resolution. This is why ECEF is `double` everywhere, and is converted to Godot's `Vector3` only after the
origin has been subtracted (a small local value).

## Phase 4: Entities

**Template**: `template <typename T> T *find_node(Node *self, ...)` makes the compiler produce a separate function
for each type: `find_node<DisNetwork>(...)` and `find_node<DisGeoOrigin>(...)` become two functions from the same
code. Templates are written in headers, because the compiler must see the body where they are used.
(`src/entity/scene_lookup.h`)

**`std::unordered_map`**: a key-to-value table (like GDScript's `Dictionary`, but typed). The manager keeps remote
entities under a 64-bit key built from `entity_id`: `(site << 32) | (application << 16) | entity`.

**Structured bindings**: `for (const auto &[key, object_id] : entities)` unpacks each pair into two names. `auto`
lets the compiler deduce the type.

**`ObjectID` and `ObjectDB::get_instance`**: a user can delete a remote-entity node themselves; if a raw pointer
(`DisRemoteEntity *`) is kept, it then points to memory that no longer exists (dangling pointer, leads to a crash).
So the map stores the node's identity (`ObjectID`); on each use, `ObjectDB::get_instance(id)` returns the node if it
is still alive and `nullptr` otherwise. Same idea as `is_instance_valid()` in GDScript.

**`memnew` and `queue_free`**: Godot objects are created in C++ with `memnew(DisRemoteEntity)`, not `new` (for
Godot's memory tracking). Godot takes ownership of a node added to the tree; delete it with `queue_free()`.

**`callable_mp`**: connects a C++ method to a signal:
`network->connect("entity_state_received", callable_mp(this, &DisEntityManager::on_entity_state))`.
The method does not have to be bound in `_bind_methods`; it is not visible from GDScript.

**`friend class`**: `friend class DisEntityManager;` inside `DisRemoteEntity` lets the manager access the entity's
private fields (state, smoothing offsets) directly; they are not exposed to GDScript.

**Anonymous namespace**: `namespace { ... }` in a `.cpp` defines helpers private to that file (other files cannot
see them, so no name clashes). The modern form of C's `static` function.

**Default argument (`DEFVAL`)**: `handle_entity_state(pdu, now = -1.0)`; to show the default to GDScript as well,
`bind_method(..., DEFVAL(-1.0))`.

**Testable time**: instead of reading the time themselves, nodes can receive it from outside through
`process_entity(now)` / `process_entities(now)`. Tests set `auto_update = false` and supply the time, so cases such
as "heartbeat after 5 seconds" can be tested without waiting and with the same result every time.

**Floating point and the transmitted state**: on the wire, velocity is a `float` (32-bit). `DisEntity` also rounds
its own copy to `float` (`as_float32`) to mimic what the receiver sees; otherwise the two sides would predict
slightly differently.

## godot-geo integration: ground snapping

**Duck typing from C++**: `DisEntityManager` does not know the ground as a class; it only uses
`ground->has_method("height_at")` and `ground->call("height_at", x, z)`. `call` goes through `Variant`, so the node
can be GDScript, C# or C++; godot-dis does not depend on godot-geo. The cost: each call takes a few hundred
nanoseconds, and there is no compile-time check (a warning is issued once for a wrong node).

**Lambda**: `auto height = [ground](real_t x, real_t z) { return ...; };` is a small function defined in place.
The square brackets list the "captured" variables: the `ground` pointer is copied. It avoids writing the same call
five times.

**`std::isfinite`** (`<cmath>`): filters out NAN and infinity. `NAN == NAN` is false, so NAN is checked this way
and not with `==`.

## Phase 6: Own socket layer, CI

**Operating system API and `#ifdef`**: `src/network/udp_socket.cpp` calls the operating system's socket API
directly, without Godot: Winsock on Windows (`winsock2.h`, `closesocket`, `WSAStartup`), BSD sockets on
Linux/macOS (`sys/socket.h`, `close`). The differing parts are separated with `#ifdef _WIN32 ... #else ... #endif`;
the compiler sees only the code of the target platform. On Windows the `ws2_32` library must also be linked
(`SConstruct`).

**Why not Godot's `PacketPeerUDP`**: on Windows, Godot never enables `SO_REUSEADDR`. As a result, two DIS
applications on the same computer could not listen on port 3000 together (required for the demo). What a library
does not support was found by testing, and the needed part was written here.

**RAII and `std::unique_ptr`**: the destructor of `UdpSocket` (`~UdpSocket()`) closes the socket. `DisNetwork` holds
it with `std::unique_ptr<UdpSocket>`: `socket.reset()` deletes the object and the destructor closes the socket.
Because the resource's lifetime is tied to the object's lifetime, "forgetting to close" is not possible (Resource
Acquisition Is Initialization).

**Forbidding copying**: `UdpSocket(const UdpSocket &) = delete;` Two copies would try to close the same operating
system socket. With `= delete` the compiler does not allow copying.

**Network byte order**: `htons(port)` / `ntohs` convert the port from "host" byte order to network order
(big-endian); DIS packets themselves being big-endian follows the same convention.

**Time source bug (found with the headless demo)**: `DisEntity` measured velocity with the real clock (wall clock),
but nodes move in physics time. When running headless, Godot sometimes runs two physics steps back to back in the
same frame: microseconds in real time, 1/60 s of movement in position. The velocity estimate blew up and a PDU was
sent every frame (404 PDUs in 20 s). The publisher now accumulates physics time and uses that (80 PDUs in 20 s).
Lesson: use the same clock as the thing being measured.

**CI (GitHub Actions)**: `.github/workflows/ci.yml` builds on Windows, Linux and macOS on every push, runs the C++
tests, downloads Godot on Windows and Linux to run the GUT tests, and produces a single `addons/dis` package with
the libraries of the three platforms. The Linux part was tried locally with WSL (Ubuntu 24.04, GCC 13).

## Workaround for a Godot bug: `CallableCustom` and using a destructor as a hook
`src/editor_first_load_guard.cpp`. In Godot 4.7, the first `--headless --import` of a new project crashed at exit
when it discovered the addon during scanning (godotengine/godot#111048): the editor generates class documentation
with a deferred call, and if the editor exits immediately, this call runs after the documentation data has been
deleted and dereferences a null pointer (`nullptr`). The bug was reproduced with a minimal C extension (without
godot-cpp) to establish that it is in the engine; seeing that a class with `is_exposed` turned off does not crash
confirmed the cause.

- **`CallableCustom`**: filling Godot's `Callable` with our own C++ class. Godot keeps the callable in a signal
  connection; when the connection (or the object that owns the signal) is destroyed, the callable is deleted and
  our destructor runs. The call itself is irrelevant here: it is connected to the root window's `renamed` signal so
  that when the root window is deleted, that is, after the editor node and the documentation data are gone, the
  destructor runs and unregisters the classes early.
- **Destructor as a "deletion event"**: RAII used in reverse; tying an object's lifetime to another object to learn
  when the other one is deleted.
- **Anonymous namespace (`namespace { ... }`)**: the contents are visible only in that `.cpp` file (like `static`
  in C); no name clashes with other files.
- **The `is_valid()` lesson**: a callable without a target object is considered "invalid" by default, and
  `connect()` rejects it and deletes it immediately. The first attempt therefore showed a false success; checking in
  the log when the hook ran revealed it. Lesson: verify *why* a fix works.

## v0.1 PDUs, recording and playback (Phases 7-10)

- **Virtual functions and `override`** (`src/pdu/dis_pdu_base.h`): `DisPduBase::encode()` is `virtual`; each PDU
  class writes its own `encode() const override`. When `DisNetwork` holds a `Ref<DisPduBase>` and calls
  `pdu->get_pdu_type()`, the function of the object's real class runs (dynamic dispatch). Writing `override` is not
  mandatory, but the compiler reports an error if the signature does not match; this prevents a typo from creating a
  new function. `bind_method(&DisPduBase::encode)` is bound once in Godot; because it is virtual, a call from
  GDScript also goes to the correct subclass.
- **Template** (`checked<Pdu>()`, `dis::find_first_of_class<T>()`): writing the same code once for different types.
  The compiler produces a separate copy for each type used; type checking happens at compile time.
- **Multiple inheritance and interface** (`DisRecorder : public godot::Node, public DisPacketTap`): `DisPacketTap`
  is an "interface" with only one pure virtual function (`= 0`). `DisNetwork` does not need to know the recorder
  node, it only sees a `DisPacketTap*`; this keeps the `network/` folder from depending on `record/`.
- **Raw pointer + `ObjectID`**: `DisNetwork` keeps listeners as `(ObjectID, DisPacketTap*)` pairs and checks with
  `ObjectDB::get_instance(id)` before each call that the object is still alive. Godot objects can be deleted at a
  moment the code does not know about; a raw pointer alone carries the risk of a dangling pointer.
- **Lambdas and STL algorithms** (`std::stable_sort`, `std::lower_bound`, `std::remove_if`):
  `[](const Packet &a, const Packet &b) { return a.time < b.time; }` is a small function written in place.
  `stable_sort` keeps the order of packets with equal times; `lower_bound` does a binary search in a sorted array
  (`seek()` is instant among thousands of packets).
- **Structured bindings** (`for (const auto &[key, index] : latest)`): unpacks a `std::pair` into two names (C++17).
- **Byte order, by hand**: a pcapng file carries the byte order of the machine that wrote it (if the 0x1A2B3C4D
  "magic number" in the section header reads reversed, the file is big-endian). The `Reader` struct swaps every
  read if needed; DIS and IP/UDP headers are always big-endian (network order).
- **Internet checksum** (`internet_checksum`): the carry-around sum of 16-bit words, then its complement. The
  checksum of a correct IPv4 header comes out as 0; the test uses this. The UDP checksum prepends a "pseudo header"
  containing the addresses.
- **Godot-independent core**: `capture_format.cpp` uses only `std::vector<uint8_t>` and `std::string`, so it goes
  straight into the doctest program; Godot-specific file access (`FileAccess`, `user://`) lives in the thin classes
  on top (`DisRecording`, `DisRecorder`). The same split exists in `geo_math` and `dead_reckoning`.
- **MSVC vs GCC difference**: the `<algorithm>` include for `std::all_of` had been forgotten; MSVC found it
  indirectly through another header, GCC did not. Lesson: include the header of everything you use yourself; the
  Linux build catches this kind of error.

## Own encoder: `byte_io.h` (Phase 11)

- **Bounds-checked reader**: before every read, `byte_io::Reader` checks `at + n <= end`; on overflow `ok()` becomes
  false and all later reads return 0. A corrupt packet therefore cannot crash the parser; a single
  `if (!in.ok())` at the end of parsing is enough. open-dis-cpp's `vector::at()` terminated the program in a build
  without exceptions.
- **Float bits with `std::memcpy`**: `reinterpret_cast`ing a `float` to `uint32_t` is undefined behavior in C++
  (strict aliasing). `memcpy(&bits, &f, 4)` is the correct way; the compiler turns it into a single move
  instruction.
- **Non-template virtual "hook"**: `DisDatumPdu::has_request()` is virtual; Comment returns `false`, Data and Set
  Data return `true`. The shared `encode()` and `decode_into()` stay in one place, and one function expresses the
  difference.
- **Lessons from real data**: a round-trip test (decode, encode, same bytes?) tests not only the fields that are
  written but also the bytes that are *ignored*. This is how extra padding and a family field written as 0 were
  found in the Wireshark samples.

## Symbol visibility in a shared library (release preparation, 2026-11-02)

- **On Linux everything is exported by default**: a `.so` built with GCC exports every global function and variable
  in it as a "dynamic symbol" (in a Windows DLL, by contrast, only `__declspec(dllexport)` symbols are exported).
  Because godot-cpp links libstdc++ statically on Linux, the library exported about 4400 symbols, including
  `std::locale::id` and parts of `std::regex` (`nm -D --defined-only`).
- **Why it crashed**: when Godot loads addons, their symbols enter the global namespace. `libJSBSim.so.1`, loaded
  afterwards (it uses the system's shared `libstdc++.so.6`), bound some `std::` symbols to our copy and some to the
  system's; with two different `locale` tables mixed, the `std::regex` constructor crashed with signal 11. It was
  not seen on Windows because each DLL keeps its own CRT separate.
- **Fix, a linker version script** (`src/linux_exports.map`, `-Wl,--version-script=`): under `global:` only
  `godot_dis_library_init` (the entry point Godot looks for); `local: *;` hides everything else.
  `-Wl,--exclude-libs,ALL` additionally hides symbols that come from static libraries. Result: a single symbol.
- **Regression check**: `tools/check_exports.py` counts the exported symbols (Linux step in CI);
  `tools/check_package.py --with godot-jsbsim-x.zip` loads both addons in the same empty project and flies the
  J3Cub. With the old library this check crashes at the very first `--import` (exit -6); with the new one it
  passes.

## Class reference in the editor: `doc_classes/` (v0.2, 2026-11-08)
- **What it is**: Godot's own classes get their F1 help from XML files (one per class: brief description,
  description, every method, property, signal and constant). Since Godot 4.3 a GDExtension can hand the editor the
  same XML.
- **Generating the skeleton**: `<godot> --headless --path . --doctool . --gdextension-docs` loads the extension,
  asks it for its registered classes (whatever `_bind_methods` declared) and writes `doc_classes/<Class>.xml` with
  empty descriptions. Run again after an API change: existing descriptions are kept, new members appear empty.
- **Building it in**: a small function in `SConstruct` concatenates the XML files, compresses them with zlib and
  writes `build/gen/class_reference.gen.cpp`: two `const int` sizes and a `const unsigned char[]` with the bytes.
  At namespace scope `const` variables are private to their `.cpp` file (internal linkage); the `extern`
  declarations in `src/class_reference.h`, included by the generated file, make them visible to
  `src/class_reference.cpp`, which decompresses them and calls the editor's
  `editor_help_load_xml_from_utf8_chars_and_len`. Only `template_debug` builds get the data (SConstruct defines
  `CLASS_REFERENCE`; without it `load_class_reference()` is empty): the editor loads the debug library, and the
  release library that games use stays smaller.
- **Why not godot-cpp's own builder** (`env.GodotCPPDocData`): it generates the same array plus a global
  `static DocDataRegistration` object. A global object's constructor runs when the library is loaded, before any
  of our code ("static initialization"), and godot-cpp then always loads the docs at editor initialization. With
  docs loaded, the editor crash of godotengine/godot#111048 came back (a new project's first headless `--import`
  crashed at exit in all three runs of `tools/check_first_import.py`): the editor generates docs for the loaded XML
  after freeing its own doc data. Loading by hand lets `register_types.cpp` skip the docs exactly when
  `install_editor_first_load_guard()` reports that the guard is active.
- **Keeping it complete**: `tools/check_docs.py` fails when a registered class has no XML or a description is empty;
  CI also regenerates the XML and fails if it differs from the committed files (the API changed, the docs did not).

## Body-axis dead reckoning (v0.2, 2026-11-14)
- **Closed form with a series fallback** (`body_displacement_r1/_r2` in `src/entity/dead_reckoning.cpp`): terms such
  as `(x - sin(x)) / w^3` subtract two nearly equal numbers when `x = |w| t` is tiny, and `double` keeps only ~16
  significant digits, so the result loses almost all of them ("catastrophic cancellation"). Below `x = 1e-3` the
  code uses the first two Taylor terms instead; a test checks both sides of the switch agree.
- **Function pointers in a loop** (`tests/cpp/test_dead_reckoning.cpp`):
  `for (auto f : { &dr::body_displacement_r1, &dr::body_displacement_r2 })` — `&name` is the address of a function,
  `{ ... }` makes a `std::initializer_list` of them (both have the same signature, so one type), and `f(omega, dt)`
  calls through the pointer. A small way to run the same check on several functions.
- **Testing a formula without an oracle:** Open-DIS Python's body-axis functions do not rotate the velocity, so they
  cannot check ours. The test integrates the motion numerically instead (Simpson's rule over 2000 steps): a
  different method that must give the same answer, which catches a wrong sign or coefficient.

## Articulated parts: `DisVariableParameter` (2026-11-14)
- **A class with only static methods** (`GDREGISTER_ABSTRACT_CLASS(DisVariableParameter)`, like `DisGeo`): GDScript
  calls `DisVariableParameter.articulated_part(...)` without creating an object; "abstract" stops `new()`.
- **`VARIANT_ENUM_CAST` needs `<godot_cpp/core/binder_common.hpp>`**: the macro specialises templates declared
  there. Without the include, MSVC reports "missing type specifier" at the macro line, an error that does not point
  at the cause. Headers that already include a Godot class header (`node3d.hpp`) get it indirectly.
- **Building a record without a PDU header**: `byte_io::Writer::data()` returns the bytes written so far;
  `finish()` would write a PDU length into bytes 8–9, which belong to the record here.
