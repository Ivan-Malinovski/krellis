# krellisrc reading and writing, shared by the effect and the settings module. Qt Core only.
add_library(krellis_config STATIC krellis_config.cpp)
target_include_directories(krellis_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(krellis_config PUBLIC Qt6::Core)
target_compile_options(krellis_config PRIVATE -Wall -Wextra)
set_target_properties(krellis_config PROPERTIES POSITION_INDEPENDENT_CODE ON)
