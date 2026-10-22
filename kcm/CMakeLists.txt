# Settings module for System Settings (Window Management) and for the effect's Configure button.
find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDEClangFormat OPTIONAL)
find_package(Qt6 REQUIRED COMPONENTS Quick DBus)
find_package(KF6 REQUIRED COMPONENTS CoreAddons I18n KCMUtils)

# ECM resolves its plugin dir to lib/plugins here, which is not on the system Qt plugin path (see ../effect).
set(KDE_INSTALL_PLUGINDIR ${KRELLIS_PLUGIN_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)
add_definitions(-DTRANSLATION_DOMAIN="kcm_krellis")

kcmutils_add_qml_kcm(kcm_krellis SOURCES krellis_kcm.cpp)
target_link_libraries(kcm_krellis PRIVATE krellis_config Qt6::DBus Qt6::Quick KF6::CoreAddons KF6::I18n KF6::KCMUtils KF6::KCMUtilsQuick)
set_target_properties(kcm_krellis PROPERTIES SKIP_BUILD_RPATH TRUE INSTALL_RPATH "")

# The effect's Configure button looks its X-KDE-ConfigModule up under kwin/effects/configs.
install(FILES $<TARGET_FILE:kcm_krellis> DESTINATION ${KRELLIS_PLUGIN_DIR}/kwin/effects/configs)

# Stage the module where the nested harness's QT_PLUGIN_PATH finds it (kcmshell6 kcm_krellis).
add_custom_command(TARGET kcm_krellis POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/root/plasma/kcms/systemsettings ${CMAKE_BINARY_DIR}/root/kwin/effects/configs
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:kcm_krellis> ${CMAKE_BINARY_DIR}/root/plasma/kcms/systemsettings/kcm_krellis.so
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:kcm_krellis> ${CMAKE_BINARY_DIR}/root/kwin/effects/configs/kcm_krellis.so)
