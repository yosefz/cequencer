cmake_minimum_required(VERSION 3.20)
project(sequencer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options (for when used as submodule)
option(SEQUENCER_BUILD_MESSAGES "Build example messages library" ON)
option(SEQUENCER_BUILD_TESTS "Build tests" ON)
option(SEQUENCER_BUILD_BENCH "Build benchmarks" ON)

add_compile_options(-Wall -Wextra -Wpedantic)

# RelWithDebInfo is the build type you profile with, so it needs the same flags.
if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo)$")
    add_compile_options(-O3 -march=native)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Global include directories (helps IDE indexing)
include_directories(
        ${CMAKE_SOURCE_DIR}/libs/zbuffer/include
        ${CMAKE_SOURCE_DIR}/libs/chronicle/include
        ${CMAKE_SOURCE_DIR}/libs/config/include
        ${CMAKE_SOURCE_DIR}/libs/metrics/include
        ${CMAKE_SOURCE_DIR}/libs/sequencer/include
        ${CMAKE_SOURCE_DIR}/libs/apps/include
)

# Core libraries (always built)
add_subdirectory(libs/zbuffer)
add_subdirectory(libs/chronicle)
add_subdirectory(libs/config)
add_subdirectory(libs/metrics)
add_subdirectory(libs/sequencer)
add_subdirectory(libs/apps)

# Example messages (optional - your project will have its own)
if(SEQUENCER_BUILD_MESSAGES)
    add_subdirectory(libs/messages)
endif()

# Tests (optional)
if(SEQUENCER_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Benchmarks (optional)
if(SEQUENCER_BUILD_BENCH)
    add_subdirectory(bench)
endif()