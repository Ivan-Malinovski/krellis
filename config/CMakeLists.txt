# trellisrc reading and writing, shared by the effect and the settings module. Qt Core only.
add_library(trellis_config STATIC trellis_config.cpp)
target_include_directories(trellis_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(trellis_config PUBLIC Qt6::Core)
target_compile_options(trellis_config PRIVATE -Wall -Wextra)
set_target_properties(trellis_config PROPERTIES POSITION_INDEPENDENT_CODE ON)
