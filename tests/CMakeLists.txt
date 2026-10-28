function(krellis_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE krellis_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

krellis_test(test_tree)
krellis_test(test_model)
krellis_test(test_resize)
krellis_test(test_motion)
krellis_test(test_navigator)
krellis_test(test_drag)
krellis_test(test_keynav)
krellis_test(test_history)
krellis_test(test_chord_safemode)
krellis_test(test_fuzzy)
krellis_test(test_shake)

krellis_test(test_input_geometry)

krellis_test(test_view_rules)

krellis_test(test_more_semantics)
krellis_test(test_session)
target_sources(test_session PRIVATE ../effect/session_store.cpp)
target_include_directories(test_session PRIVATE ../effect)

add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE krellis_config Qt6::Test)
add_test(NAME test_config COMMAND test_config)

add_executable(test_theme test_theme.cpp)
target_link_libraries(test_theme PRIVATE Qt6::Gui Qt6::Test)
target_include_directories(test_theme PRIVATE ../effect)
add_test(NAME test_theme COMMAND test_theme)

add_test(NAME qml_colour_literals COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/check_qml_colors.sh ${CMAKE_CURRENT_SOURCE_DIR}/../effect)
