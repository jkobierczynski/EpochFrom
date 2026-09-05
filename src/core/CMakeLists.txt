add_library(epoch_from_core STATIC
    GaiaCatalog.h
    GaiaCatalog.cpp
    SpaceMotion.h
    SpaceMotion.cpp
    EpochFit.h
    EpochFit.cpp
    PlateSolver.h
    PlateSolver.cpp
    FitsImage.h
    FitsImage.cpp
    LinearWcs.h
    LinearWcs.cpp
    StarDetector.h
    StarDetector.cpp
    SipPolynomial.h
    EquipmentProfile.h
    EquipmentProfile.cpp
    EquipmentCalibrator.h
    EquipmentCalibrator.cpp
    Wcs.h
    Wcs.cpp
    ImageDater.h
    ImageDater.cpp
    ReportFormatting.h
    ReportFormatting.cpp
)

target_include_directories(epoch_from_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(epoch_from_core PUBLIC Qt6::Core Eigen3::Eigen PkgConfig::WCSLIB PkgConfig::CFITSIO)
