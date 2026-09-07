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
