# Sequencer core library
add_library(sequencer_core STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sequencer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/commandSystem.cpp
)
target_include_directories(sequencer_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(sequencer_core PUBLIC
        seq::config
        seq::chronicle
        seq::zbuffer
)
add_library(seq::sequencer ALIAS sequencer_core)