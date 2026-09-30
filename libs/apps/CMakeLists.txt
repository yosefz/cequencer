# Apps library
add_library(apps STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/streamApp.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sequencerApp.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/eventStreamApp.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/commander.cpp
)
target_include_directories(apps PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(apps PUBLIC
        seq::sequencer
        seq::config
        seq::chronicle
        seq::zbuffer
)
add_library(seq::apps ALIAS apps)