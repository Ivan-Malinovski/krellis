cmake_minimum_required(VERSION 3.20)
project(kwin-trellis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Core Test)
enable_testing()

add_subdirectory(core)
add_subdirectory(config)
add_subdirectory(tests)

option(TRELLIS_BUILD_EFFECT "Build the KWin effect plugin" OFF)
option(TRELLIS_BUILD_KCM "Build the settings module (needs KCMUtils and Kirigami)" ON)
if(TRELLIS_BUILD_EFFECT)
    add_subdirectory(effect)
    if(TRELLIS_BUILD_KCM)
        add_subdirectory(kcm)
    endif()
endif()
