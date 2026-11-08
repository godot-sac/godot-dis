#include "class_reference.h"

#ifdef CLASS_REFERENCE

#include <cstring>

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/gdextension_interface_loader.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

using namespace godot;

void load_class_reference() {
	PackedByteArray packed;
	packed.resize(CLASS_REFERENCE_PACKED_SIZE);
	memcpy(packed.ptrw(), CLASS_REFERENCE_PACKED, CLASS_REFERENCE_PACKED_SIZE);
	// zlib stream, as written by SConstruct (Python's zlib.compress); Godot's DEFLATE mode reads it.
	PackedByteArray xml = packed.decompress(CLASS_REFERENCE_SIZE, FileAccess::COMPRESSION_DEFLATE);
	ERR_FAIL_COND_MSG(xml.size() != CLASS_REFERENCE_SIZE, "The built-in class reference could not be unpacked.");
	gdextension_interface::editor_help_load_xml_from_utf8_chars_and_len(reinterpret_cast<const char *>(xml.ptr()),
			xml.size());
}

#else

void load_class_reference() {}

#endif
