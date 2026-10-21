function(trellis_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE trellis_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

trellis_test(test_tree)
trellis_test(test_model)
trellis_test(test_resize)
trellis_test(test_motion)
trellis_test(test_navigator)
trellis_test(test_drag)
trellis_test(test_keynav)

trellis_test(test_input_geometry)

trellis_test(test_view_rules)

trellis_test(test_more_semantics)
trellis_test(test_session)
target_sources(test_session PRIVATE ../effect/session_store.cpp)
target_include_directories(test_session PRIVATE ../effect)

add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE trellis_config Qt6::Test)
add_test(NAME test_config COMMAND test_config)
