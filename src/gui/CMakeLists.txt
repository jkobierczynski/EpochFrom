find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Windows-only .rc resources: give the .exe *file itself* an icon (File
# Explorer, a taskbar pin made before the app has ever run, a desktop
# shortcut) -- separate from, and in addition to, the QApplication
# window icon set in main.cpp/StarfieldStandaloneMain.cpp via AppIcon.h,
# which already works cross-platform with no install step. See
# packaging/windows/epochfrom-gui.rc's own comment for the distinction.
# CMake compiles a target's own .rc source with the active toolchain's RC
# compiler (rc.exe under MSVC, windres under MinGW) automatically; this is
# a no-op everywhere else, since these variables stay empty off Windows.
set(EPOCHFROM_GUI_WIN32_RC "")
set(EPOCHFROM_STARFIELD_WIN32_RC "")
if(WIN32)
    set(EPOCHFROM_GUI_WIN32_RC "${CMAKE_SOURCE_DIR}/packaging/windows/epochfrom-gui.rc")
    set(EPOCHFROM_STARFIELD_WIN32_RC "${CMAKE_SOURCE_DIR}/packaging/windows/epochfrom-starfield.rc")
endif()

add_executable(epoch_from_gui
    main.cpp
    MainWindow.h
    MainWindow.cpp
    SolveTab.h
    SolveTab.cpp
    SolveWorker.h
    SolveWorker.cpp
    CalibrateTab.h
    CalibrateTab.cpp
    CalibrateWorker.h
    CalibrateWorker.cpp
    DateTab.h
    DateTab.cpp
    DateWorker.h
    DateWorker.cpp
    GaiaTab.h
    GaiaTab.cpp
    StarfieldTab.h
    StarfieldTab.cpp
    StarfieldWorker.h
    StarfieldWorker.cpp
    StarfieldCanvas.h
    StarfieldCanvas.cpp
    ProjectBar.h
    ProjectBar.cpp
    ProjectPaths.h
    NoWheelWidgets.h
    TabLayoutHelpers.h
    FitsDirScan.h
    AppIcon.h
    resources/icon.qrc
    ${EPOCHFROM_GUI_WIN32_RC}
)
set_target_properties(epoch_from_gui PROPERTIES OUTPUT_NAME EpochFrom-gui)
target_link_libraries(epoch_from_gui PRIVATE epoch_from_core Qt6::Widgets)

# Standalone proper-motion viewer: just the Project bar + Starfield tab from
# the app above, for anyone who wants that view on its own (see
# StarfieldStandaloneMain.cpp) -- same underlying widgets, so it can't drift
# out of sync with the tab embedded in EpochFrom-gui.
add_executable(epoch_from_starfield
    StarfieldStandaloneMain.cpp
    StarfieldTab.h
    StarfieldTab.cpp
    StarfieldWorker.h
    StarfieldWorker.cpp
    StarfieldCanvas.h
    StarfieldCanvas.cpp
    ProjectBar.h
    ProjectBar.cpp
    ProjectPaths.h
    NoWheelWidgets.h
    TabLayoutHelpers.h
    AppIcon.h
    resources/icon.qrc
    ${EPOCHFROM_STARFIELD_WIN32_RC}
)
set_target_properties(epoch_from_starfield PROPERTIES OUTPUT_NAME EpochFrom-starfield)
target_link_libraries(epoch_from_starfield PRIVATE epoch_from_core Qt6::Widgets)

install(TARGETS epoch_from_gui epoch_from_starfield
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Copy scripts/ (gaia_field_query.py, and -- Windows only in practice, but
# shipped on every platform for simplicity -- ansvr-solve-field.bat) next
# to EpochFrom-gui after every build, in the build tree itself, not only
# on `cmake --install`. Both GaiaTab::findScript() and
# SolveTab::findAnsvrWrapper() look for their file at a handful of paths
# relative to wherever the running .exe actually sits, and the very first
# (and most reliable) one either checks is "right beside me, in a scripts/
# subfolder" -- confirmed missing from a real Windows package built by
# running windeployqt directly against the raw build/install tree, which
# never had a reason to know this directory needed to come along too. A
# POST_BUILD step (rather than relying solely on the install() rule below)
# means this works for that flow as well as a `cmake --install` one, with
# no separate CI copy step required either way.
add_custom_command(TARGET epoch_from_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/scripts"
            "$<TARGET_FILE_DIR:epoch_from_gui>/scripts"
    COMMENT "Copying scripts/ next to EpochFrom-gui for GaiaTab/SolveTab auto-detection")
install(DIRECTORY ${CMAKE_SOURCE_DIR}/scripts/
        DESTINATION ${CMAKE_INSTALL_BINDIR}/scripts)

# Same story, same fix, for tools/ (just residual-field.html today):
# MainWindow::findResidualFieldViewer() already looks for it at "right
# beside me, in a tools/ subfolder" first -- same mechanism as
# GaiaTab::findScript()/SolveTab::findAnsvrWrapper() above -- but nothing
# copied it there, so a windeployqt-built Windows package (or any
# `cmake --install` tree) was missing it even though the Tools menu's
# auto-detection code was already written to expect it. Without this, the
# Tools menu's "Couldn't find tools/residual-field.html automatically"
# fallback message is what everyone actually got.
add_custom_command(TARGET epoch_from_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/tools"
            "$<TARGET_FILE_DIR:epoch_from_gui>/tools"
    COMMENT "Copying tools/ next to EpochFrom-gui for MainWindow's Residual Field Viewer auto-detection")
# Same story again for the standalone CLI tool (EpochFrom / EpochFrom.exe,
# the epoch_from_cli target defined in src/cli/CMakeLists.txt -- see that
# file for the matching install() rule this pairs with): it builds into its
# own separate src/cli build directory, not next to EpochFrom-gui, so a
# Windows package assembled by running windeployqt directly against
# EpochFrom-gui's own build/install directory (as this project's Windows CI
# does) never picked it up at all -- the GUI shipped, but the CLI it's
# meant to sit alongside for anyone scripting a batch run didn't.
# $<TARGET_FILE:epoch_from_cli> (and add_dependencies below) makes sure
# epoch_from_cli is actually built before this copy runs.
add_custom_command(TARGET epoch_from_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:epoch_from_cli>"
            "$<TARGET_FILE_DIR:epoch_from_gui>/$<TARGET_FILE_NAME:epoch_from_cli>"
    COMMENT "Copying the EpochFrom CLI tool next to EpochFrom-gui")
add_dependencies(epoch_from_gui epoch_from_cli)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/tools/
        DESTINATION ${CMAKE_INSTALL_BINDIR}/tools)

# Desktop-entry + hicolor-icon-theme install, so a proper `cmake --install`
# actually gets EpochFrom a taskbar/dock icon (see the comment on
# include(GNUInstallDirs) in the top-level CMakeLists.txt for why the
# in-process QIcon set via setWindowIcon() isn't enough on its own). XDG
# desktop entries are a Linux/freedesktop.org convention -- Windows and
# macOS have their own, separate icon-embedding mechanisms this doesn't
# attempt to cover.
if(UNIX AND NOT APPLE)
    install(FILES
                ${CMAKE_SOURCE_DIR}/packaging/linux/epochfrom-gui.desktop
                ${CMAKE_SOURCE_DIR}/packaging/linux/epochfrom-starfield.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

    # The .desktop files above reference Icon=epochfrom, resolved through
    # the standard hicolor icon theme's per-size "apps" directories -- the
    # square PNGs already rasterized for the in-app QIcon (AppIcon.h) cover
    # every fixed size a theme would otherwise interpolate, and the SVG
    # gives crisp results at anything larger still.
    foreach(_epochfrom_icon_size 16 24 32 48 64 128 256 512)
        install(FILES resources/icons/epochfrom-${_epochfrom_icon_size}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_epochfrom_icon_size}x${_epochfrom_icon_size}/apps
                RENAME epochfrom.png)
    endforeach()
    install(FILES ${CMAKE_SOURCE_DIR}/docs/images/epochfrom-logo.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
            RENAME epochfrom.svg)
endif()
