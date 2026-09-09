add_executable(epoch_from_cli main.cpp)
set_target_properties(epoch_from_cli PROPERTIES OUTPUT_NAME EpochFrom)
target_link_libraries(epoch_from_cli PRIVATE epoch_from_core Qt6::Core)

# Was never installed at all -- src/gui/CMakeLists.txt has its own
# install(TARGETS epoch_from_gui epoch_from_starfield ...) rule, but nothing
# equivalent existed for the CLI, so `cmake --install` silently left
# EpochFrom(.exe) out of the install tree entirely. See src/gui/CMakeLists.txt
# for the other half of this fix (the raw-build-tree POST_BUILD copy next to
# EpochFrom-gui, needed for the same reason scripts/ and tools/ needed one --
# a Windows package built by running windeployqt directly against the build
# tree never had a reason to know this binary needed to come along too).
install(TARGETS epoch_from_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
