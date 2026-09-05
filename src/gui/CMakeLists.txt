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
    ProjectBar.h
    ProjectBar.cpp
    ProjectPaths.h
    NoWheelWidgets.h
    TabLayoutHelpers.h
    FitsDirScan.h
)
set_target_properties(epoch_from_gui PROPERTIES OUTPUT_NAME EpochFrom-gui)
target_link_libraries(epoch_from_gui PRIVATE epoch_from_core Qt6::Widgets)
