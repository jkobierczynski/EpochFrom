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

find_package(Qt6 REQUIRED COMPONENTS Core)
# Confirmed on a real Debian build: Eigen's own generated
# Eigen3ConfigVersion.cmake has a long-standing bug in its custom version
# check -- CMake's find_package() version-RANGE syntax ("3.3...5.0") is
# rejected outright by that script even against a real, plainly-compatible
# installed version (Debian's system Eigen3Config.cmake reports 3.4.0,
# "considered but not accepted" for this exact range). Two independent
# upstream reports confirm the same failure and the same fix: ranges don't
# work through Eigen's ConfigVersion script at all, but two ordinary,
# non-range find_package() calls do, because that's the well-tested code
# path through the same script.
# https://gitlab.com/libeigen/eigen/-/issues/2972
# https://gitlab.com/libeigen/eigen/-/issues/2994
# A bare minimum-version request was avoided here for a reason, though:
# vcpkg's eigen3 port jumped to the real, deliberate Eigen 5.0 release, whose
# ConfigVersion script uses a same-major-version compatibility check -- a
# bare "3.3" request rejects 5.0.1 outright ("version found is not
# compatible") even though nothing here actually needs anything 3.3 added
# over 5.0. So: try 3.3 first (matches any real-world 3.x install, Debian's
# 3.4.0 included) and only fall back to asking for 5.0 outright if that
# wasn't found (matches vcpkg's Eigen 5.0 port via its own same-major
# check). C++17 (already required above) satisfies Eigen 5.0's own minimum.
# Eigen 5.0's breaking changes are narrow and don't touch ordinary
# matrix/vector/solver usage -- watch for them specifically if a future
# compile error mentions Eigen::all/last (moved to
# Eigen::placeholders::all/last) or a runtime (rather than compile-time
# template) SVD ComputeThinU/ComputeFullV option.
find_package(Eigen3 3.3 QUIET NO_MODULE)
if(NOT Eigen3_FOUND)
    find_package(Eigen3 5.0 REQUIRED NO_MODULE)
endif()

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

find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(WCSLIB IMPORTED_TARGET wcslib)
    if(WCSLIB_FOUND)
        add_library(EpochFrom::WCSLIB ALIAS PkgConfig::WCSLIB)
        set(EPOCHFROM_WCSLIB_FOUND TRUE)
    endif()

    pkg_check_modules(CFITSIO IMPORTED_TARGET cfitsio)
    if(CFITSIO_FOUND)
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
    # Confirmed against the actual vcpkg port in use (cfitsio 4.7.0): its
    # upstream CMakeLists.txt sets LIB_NAME to the unnamespaced `cfitsio`
    # but exports it via `install(EXPORT cfitsioTargets NAMESPACE CFITSIO::
    # ...)`, so the real target is `CFITSIO::cfitsio` -- matching neither
    # of the two guesses this file previously made (see docs/windows-port.md
    # for the "needs confirming" note this resolves; a real MSVC/vcpkg build
    # got CFITSIO_DIR set correctly by find_package() above, proving the
    # config was located, while both TARGET checks below still failed).
    # Checked first since it's the one now confirmed current; the other two
    # stay as fallbacks in case a different vcpkg/registry snapshot ever
    # exports it differently again.
    if(TARGET CFITSIO::cfitsio)
        add_library(EpochFrom::CFITSIO ALIAS CFITSIO::cfitsio)
        set(EPOCHFROM_CFITSIO_FOUND TRUE)
    elseif(TARGET cfitsio)
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
        "  Windows + MSYS2/MinGW: pacman -S mingw-w64-x86_64-cfitsio (and build\n"
        "    wcslib from source into the same MSYS2 prefix -- it isn't packaged there).\n"
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
