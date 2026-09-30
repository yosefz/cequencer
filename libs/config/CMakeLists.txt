file(GLOB SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/include/config/*.hpp)

add_library(config STATIC ${SOURCES} ${HEADERS}
        src/configLoader.cpp
        include/config/config.hpp
        include/config/configLoader.hpp
)
target_include_directories(config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_library(seq::config ALIAS config)