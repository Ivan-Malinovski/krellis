find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
find_package(Qt6 REQUIRED COMPONENTS Gui Widgets Quick DBus)
find_package(KWin REQUIRED)
find_package(KDecoration3 REQUIRED)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6GlobalAccel REQUIRED)
find_package(KF6ColorScheme REQUIRED)
find_package(KF6Config REQUIRED)

set(CMAKE_AUTORCC ON)
# The plugin id doubles as the file name and the resource prefix. KWin never re-reads a plugin file it has already
# mapped, so tools/install.sh builds each live install under a fresh id (krellis_<stamp>) and the new code loads
# without a logout. The default id is what the nested harness uses.
set(KRELLIS_ID krellis CACHE STRING "Effect plugin id, also the .so file name")
# The effect's Configure button opens the settings module (kcm/), looked up under kwin/effects/configs.
if(KRELLIS_BUILD_KCM)
    set(KRELLIS_CONFIG_MODULE ",\n    \"X-KDE-ConfigModule\": \"kcm_krellis\"")
endif()
configure_file(metadata.json.in ${CMAKE_CURRENT_BINARY_DIR}/metadata.json @ONLY)
configure_file(krellis.qrc.in ${CMAKE_CURRENT_BINARY_DIR}/${KRELLIS_ID}.qrc @ONLY)
add_library(krellis MODULE motion_overlay.cpp native_resize.cpp krellis_effect.cpp multicanvas.cpp surface_input.cpp view_rules.cpp persistence.cpp palette.cpp pin.cpp tabs.cpp session_store.cpp theme.cpp boot_guard.cpp ${CMAKE_CURRENT_BINARY_DIR}/${KRELLIS_ID}.qrc)
target_include_directories(krellis PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
# The plugin only loads into the KWin it was built against (see core/kwin_match.h); the source directory is named in
# the notification that tells the user to rebuild.
target_compile_definitions(krellis PRIVATE KRELLIS_ID="${KRELLIS_ID}" KRELLIS_KWIN_VERSION="${KWin_VERSION}" KRELLIS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(krellis PRIVATE krellis_core krellis_config KWin::kwin KDecoration3::KDecoration KF6::CoreAddons KF6::GlobalAccel KF6::ColorScheme KF6::ConfigCore KF6::ConfigGui Qt6::DBus Qt6::Quick)
set_target_properties(krellis PROPERTIES PREFIX "" OUTPUT_NAME ${KRELLIS_ID})
# Stage a plugin root the nested harness can point QT_PLUGIN_PATH at.
add_custom_command(TARGET krellis POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/root/kwin/effects/plugins
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:krellis> ${CMAKE_BINARY_DIR}/root/kwin/effects/plugins/${KRELLIS_ID}.so.new
    COMMAND ${CMAKE_COMMAND} -E rename ${CMAKE_BINARY_DIR}/root/kwin/effects/plugins/${KRELLIS_ID}.so.new ${CMAKE_BINARY_DIR}/root/kwin/effects/plugins/${KRELLIS_ID}.so)

# Explicit test-only target. It observes real frame buffers without driving damage.
add_library(krellis_paint_probe MODULE EXCLUDE_FROM_ALL ../scenarios/paint-probe.cpp)
target_link_libraries(krellis_paint_probe PRIVATE KWin::kwin Qt6::DBus Qt6::Quick KF6::CoreAddons)
set_target_properties(krellis_paint_probe PROPERTIES PREFIX "" OUTPUT_NAME krellis_paint_probe)
add_custom_command(TARGET krellis_paint_probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/root/kwin/effects/plugins
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:krellis_paint_probe> ${CMAKE_BINARY_DIR}/root/kwin/effects/plugins/krellis_paint_probe.so.new
    COMMAND ${CMAKE_COMMAND} -E rename ${CMAKE_BINARY_DIR}/root/kwin/effects/plugins/krellis_paint_probe.so.new ${CMAKE_BINARY_DIR}/root/kwin/effects/plugins/krellis_paint_probe.so)

# Installs next to KWin's own effects; KWin only finds plugins under the system Qt plugin path.
# ECM resolves its plugin dir to lib/plugins here, which is not on KWin's search path.
set(KRELLIS_PLUGIN_DIR lib/qt6/plugins CACHE STRING "Qt plugin dir, relative to the install prefix")
install(TARGETS krellis DESTINATION ${KRELLIS_PLUGIN_DIR}/kwin/effects/plugins)
set_target_properties(krellis PROPERTIES SKIP_BUILD_RPATH TRUE INSTALL_RPATH "")
