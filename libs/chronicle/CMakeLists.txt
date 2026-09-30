file(GLOB SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/include/holocron/*.hpp)

add_library(chronicle STATIC ${SOURCES} ${HEADERS}
        src/appender.cpp
        src/mappedFile.cpp
        src/tailer.cpp
        include/holocron/types.hpp
        include/holocron/mappedFile.hpp
        include/holocron/appender.hpp
        include/holocron/tailer.hpp
        include/holocron/holocronQueue.hpp)
target_include_directories(chronicle PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(chronicle PUBLIC seq::zbuffer)
add_library(seq::chronicle ALIAS chronicle)