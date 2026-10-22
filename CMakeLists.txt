cmake_minimum_required(VERSION 3.20)
project(krellis VERSION 0.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test)
enable_testing()

add_subdirectory(core)
add_subdirectory(config)
add_subdirectory(tests)

option(KRELLIS_BUILD_EFFECT "Build the KWin effect plugin" OFF)
option(KRELLIS_BUILD_KCM "Build the settings module (needs KCMUtils and Kirigami)" ON)
if(KRELLIS_BUILD_EFFECT)
    add_subdirectory(effect)
    if(KRELLIS_BUILD_KCM)
        add_subdirectory(kcm)
    endif()
endif()
