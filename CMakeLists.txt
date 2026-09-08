cmake_minimum_required(VERSION 3.20)
project(epoch_from VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# CMAKE_INSTALL_BINDIR/DATADIR etc. for `cmake --install` -- currently used
# by src/gui/CMakeLists.txt to install the .desktop files and hicolor-theme
# icons that get EpochFrom-gui's taskbar/dock icon showing (see that file
# and README's "Desktop integration" section for why those are needed at
# all: the window's own icon, set via QApplication::setWindowIcon(), is
# correct on the wire the moment you run the binary, but most desktop
# environments' taskbar/dock -- GNOME Shell/Ubuntu Dock chief among them --
# resolve *that* icon by matching the window to an installed .desktop
# entry, not by reading the window's live icon pixels, so without one
# installed they fall back to a generic icon even though nothing is wrong
# with the app itself).
include(GNUInstallDirs)

# Single-config convenience: default to a Release build if the user didn't ask.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# Real-world confirmation (a built EpochFrom-gui.exe run standalone on
# Windows): MinGW links against its own runtime as shared DLLs by default
# (libgcc_s_seh-1.dll, libstdc++-6.dll, libwinpthread-1.dll) -- windeployqt
# only knows about Qt's own dependencies, not the toolchain's, so it never
# copies these, and the .exe fails to start ("libgcc_s_seh-1.dll was not
# found") anywhere they aren't already on PATH (i.e. everywhere outside an
# MSYS2 shell). Statically linking them in is the standard fix for exactly
# this MinGW+Qt deployment gap -- applies to every target below, Qt's own
# DLLs (brought in via windeployqt) and wcslib/cfitsio's are unaffected,
# since those are still linked as the shared libraries their find_package()/
# pkg-config imported targets specify.
if(MINGW)
    add_link_options(-static-libgcc -static-libstdc++ -static -lwinpthread)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core)

# No minimum version pinned here on purpose: Eigen upstream jumped straight
# from 3.4 to a 5.0 release as a version-scheme realignment, and CMake's
# package version check (SameMajorVersion compatibility) treats that 5.x as
# incompatible with an "Eigen3 3.3" request even though it's a superset of
# what EpochFrom actually uses -- MSYS2's eigen3 package already ships 5.0.1,
# so pinning 3.3 broke the MinGW build. Nothing here needs anything newer
# than what's been in Eigen since well before 3.3.
find_package(Eigen3 REQUIRED NO_MODULE)

# wcslib and cfitsio, resolved into two OS-agnostic targets --
# EpochFrom::WCSLIB and EpochFrom::CFITSIO -- that the rest of the build
# (src/core/CMakeLists.txt) links against without caring which of the
# paths below actually found them. pkg-config covers Linux and Windows
# via MSYS2/MinGW (both ship pkgconf and both libraries as regular
# packages); it's tried first since that's also what every dev machine
# this project has actually been built on uses. Neither library has an
# official vcpkg port with a guaranteed CMake config as of this writing,
# so an MSVC/vcpkg build falls back to a plain find_package() --
# cmake/FindWCSLIB.cmake for wcslib (see that file for the search hints
# it honors), and cfitsio's own upstream CMake config for cfitsio -- which
# means a manually-built wcslib pointed at via CMAKE_PREFIX_PATH is
# currently the expected path on MSVC. See docs/windows-port.md for the
# full story, including what's untested. Everything below the pkg-config
# block is Windows-only in practice: on Linux, pkg-config finding both
# leaves EPOCHFROM_WCSLIB_FOUND/EPOCHFROM_CFITSIO_FOUND already TRUE and
# skips straight to the FATAL_ERROR guard, which never fires.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

set(EPOCHFROM_WCSLIB_FOUND FALSE)
set(EPOCHFROM_CFITSIO_FOUND FALSE)

# Some MSYS2 packages' .pc files (cfitsio's, confirmed by CI; possibly
# others) hardcode a literal POSIX-style "prefix=/mingw64" rather than a
# resolved absolute path. That's only meaningful inside the MSYS2 runtime's
# own path translation -- cmake.exe is a native (non-MSYS) binary, so it
# takes "/mingw64/include" literally, and either fails outright at generate
# time (a non-existent INTERFACE_INCLUDE_DIRECTORIES entry) or would
# silently produce a "-L/mingw64/lib" nothing can find at link time.
# Rewrite any such path pkg-config hands back to the real MinGW root,
# derived from where the active compiler actually lives (always correct,
# regardless of where a given checkout happens to sit on disk).
function(_epochfrom_fix_mingw_pkgconfig_paths target)
    if(NOT MINGW OR NOT TARGET ${target})
        return()
    endif()
    get_filename_component(_epochfrom_mingw_bindir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_epochfrom_mingw_root "${_epochfrom_mingw_bindir}" DIRECTORY)
    foreach(_epochfrom_prop
            INTERFACE_INCLUDE_DIRECTORIES
            INTERFACE_LINK_DIRECTORIES
            INTERFACE_LINK_LIBRARIES
            INTERFACE_LINK_OPTIONS)
        get_target_property(_epochfrom_value ${target} ${_epochfrom_prop})
        if(_epochfrom_value)
            set(_epochfrom_fixed "")
            foreach(_epochfrom_item ${_epochfrom_value})
                if(_epochfrom_item MATCHES "^-L/mingw(32|64)(/.*)?$")
                    string(REGEX REPLACE "^-L/mingw(32|64)" "-L${_epochfrom_mingw_root}" _epochfrom_item "${_epochfrom_item}")
                elseif(_epochfrom_item MATCHES "^/mingw(32|64)(/.*)?$")
                    string(REGEX REPLACE "^/mingw(32|64)" "${_epochfrom_mingw_root}" _epochfrom_item "${_epochfrom_item}")
                endif()
                list(APPEND _epochfrom_fixed "${_epochfrom_item}")
            endforeach()
            set_target_properties(${target} PROPERTIES ${_epochfrom_prop} "${_epochfrom_fixed}")
        endif()
    endforeach()
endfunction()

find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(WCSLIB IMPORTED_TARGET wcslib)
    if(WCSLIB_FOUND)
        _epochfrom_fix_mingw_pkgconfig_paths(PkgConfig::WCSLIB)
        add_library(EpochFrom::WCSLIB ALIAS PkgConfig::WCSLIB)
        set(EPOCHFROM_WCSLIB_FOUND TRUE)
    endif()

    pkg_check_modules(CFITSIO IMPORTED_TARGET cfitsio)
    if(CFITSIO_FOUND)
        _epochfrom_fix_mingw_pkgconfig_paths(PkgConfig::CFITSIO)
        add_library(EpochFrom::CFITSIO ALIAS PkgConfig::CFITSIO)
        set(EPOCHFROM_CFITSIO_FOUND TRUE)
    endif()
endif()

if(NOT EPOCHFROM_WCSLIB_FOUND)
    find_package(WCSLIB QUIET)
    if(WCSLIB_FOUND)
        add_library(EpochFrom::WCSLIB ALIAS WCSLIB::WCSLIB)
        set(EPOCHFROM_WCSLIB_FOUND TRUE)
    endif()
endif()

if(NOT EPOCHFROM_CFITSIO_FOUND)
    find_package(CFITSIO CONFIG QUIET)
    if(TARGET cfitsio)
        # vcpkg's cfitsio port (where present) has historically exported a
        # plain, unnamespaced `cfitsio` target rather than `CFITSIO::CFITSIO`.
        add_library(EpochFrom::CFITSIO ALIAS cfitsio)
        set(EPOCHFROM_CFITSIO_FOUND TRUE)
    elseif(TARGET CFITSIO::CFITSIO)
        add_library(EpochFrom::CFITSIO ALIAS CFITSIO::CFITSIO)
        set(EPOCHFROM_CFITSIO_FOUND TRUE)
    endif()
endif()

if(NOT EPOCHFROM_WCSLIB_FOUND OR NOT EPOCHFROM_CFITSIO_FOUND)
    message(FATAL_ERROR
        "Could not locate wcslib and/or cfitsio.\n"
        "  Linux: install the -dev packages (see README's Building section).\n"
        "  Windows + MSYS2/MinGW: pacman -S mingw-w64-x86_64-cfitsio mingw-w64-x86_64-wcslib\n"
        "  Windows + MSVC/vcpkg: vcpkg install cfitsio; wcslib has no vcpkg port,\n"
        "    build it manually and add its install prefix to CMAKE_PREFIX_PATH.\n"
        "  See docs/windows-port.md for the current state of all of this.")
endif()

# The GUI (src/gui, "EpochFrom-gui") is a thin Qt Widgets front end over the
# same core library the CLI uses -- optional at configure time (default on)
# so a headless/CI build, or a machine that only has qt6-base-dev and not
# qt6-base-dev-tools' widgets module, can still build everything else.
option(EPOCHFROM_BUILD_GUI "Build the EpochFrom-gui desktop application" ON)
if(EPOCHFROM_BUILD_GUI)
    find_package(Qt6 QUIET COMPONENTS Widgets)
    if(NOT Qt6Widgets_FOUND)
        message(STATUS "Qt6::Widgets not found -- skipping EpochFrom-gui (install qt6-base-dev's "
                        "widgets module to enable it, or pass -DEPOCHFROM_BUILD_GUI=OFF to silence "
                        "this message)")
        set(EPOCHFROM_BUILD_GUI OFF)
    endif()
endif()

enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/cli)
if(EPOCHFROM_BUILD_GUI)
    add_subdirectory(src/gui)
endif()
add_subdirectory(tests)
