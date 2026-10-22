add_library(krellis_core STATIC
    tree.cpp
    document.cpp
    spatial.cpp
    resize.cpp
    motion.cpp
    builder.cpp
    navigator.cpp
    drag.cpp
    session.cpp
    keynav.cpp
    reconcile.cpp
)
target_include_directories(krellis_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(krellis_core PRIVATE -Wall -Wextra)
set_target_properties(krellis_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
