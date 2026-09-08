add_executable(epoch_from_cli main.cpp)
set_target_properties(epoch_from_cli PROPERTIES OUTPUT_NAME EpochFrom)
target_link_libraries(epoch_from_cli PRIVATE epoch_from_core Qt6::Core)

# Confirmed gap while setting up CI packaging: unlike src/gui/CMakeLists.txt's
# two targets, this one had no install() rule at all, so a plain `cmake
# --install` (what both the README's Desktop-integration section and the
# release workflow use to stage a distributable tree) silently produced a
# tree with EpochFrom-gui/EpochFrom-starfield but no EpochFrom CLI binary in
# it. Matches the GUI targets' own install rule exactly.
install(TARGETS epoch_from_cli
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
