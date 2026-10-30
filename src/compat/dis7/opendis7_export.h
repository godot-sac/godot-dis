// open-dis-cpp normally gets this header from CMake's generate_export_header().
// We compile open-dis-cpp statically into the GDExtension, so the export macros are empty.
#pragma once

#define OPENDIS7_EXPORT
#define OPENDIS7_NO_EXPORT
#define OPENDIS7_DEPRECATED
#define OPENDIS7_DEPRECATED_EXPORT
#define OPENDIS7_DEPRECATED_NO_EXPORT
