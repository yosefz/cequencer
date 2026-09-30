enable_testing()

# Chronicle tests (you have these)
add_executable(test_zbuffer test_zbuffer.cpp)
target_link_libraries(test_zbuffer PRIVATE seq::zbuffer)
add_test(NAME test_zbuffer COMMAND test_zbuffer)

add_executable(test_chronicle test_chronicle.cpp)
target_link_libraries(test_chronicle PRIVATE seq::chronicle)
add_test(NAME test_chronicle COMMAND test_chronicle)

add_executable(test_all test_all.cpp)
target_link_libraries(test_all PRIVATE seq::chronicle)
add_test(NAME test_all COMMAND test_all)

# Config tests
add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE seq::config)
add_test(NAME test_config COMMAND test_config)

# Metrics tests
add_executable(test_metrics test_metrics.cpp)
target_link_libraries(test_metrics PRIVATE seq::metrics)
add_test(NAME test_metrics COMMAND test_metrics)

# Sequencer tests (seq::sequencer brings in config, chronicle, zbuffer)
add_executable(test_sequencer test_sequencer.cpp)
target_link_libraries(test_sequencer PRIVATE seq::sequencer)
add_test(NAME test_sequencer COMMAND test_sequencer)

# Apps tests (seq::apps brings in sequencer, config, chronicle, zbuffer)
add_executable(test_apps test_apps.cpp)
target_link_libraries(test_apps PRIVATE seq::apps)
add_test(NAME test_apps COMMAND test_apps)

# Only build message tests if example messages are built
if(SEQUENCER_BUILD_MESSAGES)
    add_executable(test_messages test_messages.cpp)
    target_link_libraries(test_messages PRIVATE seq::messages seq::chronicle)
    add_test(NAME test_messages COMMAND test_messages)
endif()