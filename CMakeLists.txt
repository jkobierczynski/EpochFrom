cmake_minimum_required(VERSION 3.20)
project(epoch_from VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Single-config convenience: default to a Release build if the user didn't ask.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Eigen3 3.3 REQUIRED NO_MODULE)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WCSLIB REQUIRED IMPORTED_TARGET wcslib)
pkg_check_modules(CFITSIO REQUIRED IMPORTED_TARGET cfitsio)

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
