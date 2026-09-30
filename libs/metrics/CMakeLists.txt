# Metrics core library
# Leaf target: the recording types are header-only and time is passed in by the
# caller, so this deliberately links nothing.
add_library(metrics_core STATIC
        src/registry.cpp
)
target_include_directories(metrics_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_library(seq::metrics ALIAS metrics_core)
