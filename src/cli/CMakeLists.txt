add_executable(epoch_from_cli main.cpp)
set_target_properties(epoch_from_cli PROPERTIES OUTPUT_NAME EpochFrom)
target_link_libraries(epoch_from_cli PRIVATE epoch_from_core Qt6::Core)
