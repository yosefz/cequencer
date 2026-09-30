file(GLOB SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/include/zbuffer/*.hpp)

add_library(zbuffer STATIC ${SOURCES} ${HEADERS})
target_include_directories(zbuffer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_library(seq::zbuffer ALIAS zbuffer)