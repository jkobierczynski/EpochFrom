find_package(Qt6 REQUIRED COMPONENTS Widgets)

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
)
set_target_properties(epoch_from_starfield PROPERTIES OUTPUT_NAME EpochFrom-starfield)
target_link_libraries(epoch_from_starfield PRIVATE epoch_from_core Qt6::Widgets)

install(TARGETS epoch_from_gui epoch_from_starfield
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

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
