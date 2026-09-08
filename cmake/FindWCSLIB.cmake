#[=======================================================================[
FindWCSLIB.cmake
------------------------------------------------------------------------

Locates wcslib (https://www.atnf.csiro.au/people/mcalabre/WCS/) for the
one case where the top-level CMakeLists.txt's pkg-config path can't find
it -- chiefly an MSVC/vcpkg build on Windows, since wcslib has no vcpkg
port and is typically built from source by hand there. Linux and
MSYS2/MinGW builds both resolve wcslib via pkg-config instead and never
reach this module (see the top-level CMakeLists.txt).

wcslib's own headers are included flatly throughout this project, e.g.
`#include <wcs.h>` rather than `#include <wcslib/wcs.h>` -- that matches
how its own `wcslib.pc` sets up `-I<prefix>/include/wcslib`, so this
module searches for `wcs.h` under a `wcslib/` subdirectory of the usual
locations and reports that subdirectory itself as the include path.

Honors the standard CMake search hints, so a manually-built wcslib needs
nothing beyond:

    cmake -B build -S . -DCMAKE_PREFIX_PATH=<path to wcslib's install prefix>

or the WCSLIB_ROOT environment/cache variable, if you'd rather not fold it
into CMAKE_PREFIX_PATH.

Defines, on success:
    WCSLIB_FOUND          -- TRUE if both the header and library were found
    WCSLIB_INCLUDE_DIR    -- directory containing wcs.h
    WCSLIB_LIBRARY        -- full path to the wcs library
    WCSLIB::WCSLIB         -- imported target (link against this)
#]=======================================================================]

find_path(WCSLIB_INCLUDE_DIR
    NAMES wcs.h
    PATH_SUFFIXES wcslib
    HINTS ${WCSLIB_ROOT} ENV WCSLIB_ROOT
    DOC "Directory containing wcslib's wcs.h"
)

# Library name varies by toolchain: MSYS2/MinGW and Unix builds typically
# produce libwcs.{a,so,dylib} (found as "wcs"); an MSVC build of wcslib
# commonly names the import library wcslib.lib instead -- both names are
# tried so either toolchain's output is found without extra configuration.
find_library(WCSLIB_LIBRARY
    NAMES wcs libwcs wcslib
    HINTS ${WCSLIB_ROOT} ENV WCSLIB_ROOT
    DOC "Path to the wcslib library"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WCSLIB
    REQUIRED_VARS WCSLIB_LIBRARY WCSLIB_INCLUDE_DIR
)

if(WCSLIB_FOUND AND NOT TARGET WCSLIB::WCSLIB)
    add_library(WCSLIB::WCSLIB UNKNOWN IMPORTED)
    set_target_properties(WCSLIB::WCSLIB PROPERTIES
        IMPORTED_LOCATION "${WCSLIB_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${WCSLIB_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(WCSLIB_INCLUDE_DIR WCSLIB_LIBRARY)
