find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
find_package(Qt6 REQUIRED COMPONENTS Gui Widgets Quick DBus)
find_package(KWin REQUIRED)
find_package(KDecoration3 REQUIRED)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6GlobalAccel REQUIRED)

set(CMAKE_AUTORCC ON)
# The plugin id doubles as the file name and the resource prefix. KWin never re-reads a plugin file it has already
# mapped, so tools/install.sh builds each live install under a fresh id (trellis_<stamp>) and the new code loads
# without a logout. The default id is what the nested harness uses.
set(TRELLIS_ID trellis CACHE STRING "Effect plugin id, also the .so file name")
configure_file(metadata.json.in ${CMAKE_CURRENT_BINARY_DIR}/metadata.json @ONLY)
configure_file(trellis.qrc.in ${CMAKE_CURRENT_BINARY_DIR}/${TRELLIS_ID}.qrc @ONLY)
add_library(trellis MODULE native_resize.cpp trellis_effect.cpp multicanvas.cpp surface_input.cpp view_rules.cpp persistence.cpp session_store.cpp ${CMAKE_CURRENT_BINARY_DIR}/${TRELLIS_ID}.qrc)
target_include_directories(trellis PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(trellis PRIVATE TRELLIS_ID="${TRELLIS_ID}")
target_link_libraries(trellis PRIVATE trellis_core KWin::kwin KDecoration3::KDecoration KF6::CoreAddons KF6::GlobalAccel Qt6::DBus Qt6::Quick)
set_target_properties(trellis PROPERTIES PREFIX "" OUTPUT_NAME ${TRELLIS_ID})
# Stage a plugin root the nested harness can point QT_PLUGIN_PATH at.
add_custom_command(TARGET trellis POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/root/kwin/effects/plugins
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:trellis> ${CMAKE_BINARY_DIR}/root/kwin/effects/plugins/${TRELLIS_ID}.so.new
    COMMAND ${CMAKE_COMMAND} -E rename ${CMAKE_BINARY_DIR}/root/kwin/effects/plugins/${TRELLIS_ID}.so.new ${CMAKE_BINARY_DIR}/root/kwin/effects/plugins/${TRELLIS_ID}.so)

# Explicit test-only target. It observes real frame buffers without driving damage.
add_library(trellis_paint_probe MODULE EXCLUDE_FROM_ALL ../scenarios/paint-probe.cpp)
target_link_libraries(trellis_paint_probe PRIVATE KWin::kwin Qt6::DBus Qt6::Quick KF6::CoreAddons)
set_target_properties(trellis_paint_probe PROPERTIES PREFIX "" OUTPUT_NAME trellis_paint_probe)
add_custom_command(TARGET trellis_paint_probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/root/kwin/effects/plugins
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:trellis_paint_probe> ${CMAKE_BINARY_DIR}/root/kwin/effects/plugins/trellis_paint_probe.so.new
    COMMAND ${CMAKE_COMMAND} -E rename ${CMAKE_BINARY_DIR}/root/kwin/effects/plugins/trellis_paint_probe.so.new ${CMAKE_BINARY_DIR}/root/kwin/effects/plugins/trellis_paint_probe.so)

# Installs next to KWin's own effects; KWin only finds plugins under the system Qt plugin path.
# ECM resolves its plugin dir to lib/plugins here, which is not on KWin's search path.
set(TRELLIS_PLUGIN_DIR lib/qt6/plugins CACHE STRING "Qt plugin dir, relative to the install prefix")
install(TARGETS trellis DESTINATION ${TRELLIS_PLUGIN_DIR}/kwin/effects/plugins)
set_target_properties(trellis PROPERTIES SKIP_BUILD_RPATH TRUE INSTALL_RPATH "")
