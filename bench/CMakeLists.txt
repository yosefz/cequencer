# Threading support (must come before targets that use it)
find_package(Threads REQUIRED)

# Chronicle benchmarks (you have these)
add_executable(bench_chronicle bench_chronicle.cpp)
target_link_libraries(bench_chronicle PRIVATE seq::chronicle)

add_executable(bench_concurrent bench_concurrent.cpp)
target_link_libraries(bench_concurrent PRIVATE seq::chronicle Threads::Threads)

# Sequencer framework benchmarks (seq::apps brings in all dependencies)
add_executable(bench_sequencer bench_sequencer.cpp)
target_link_libraries(bench_sequencer PRIVATE seq::apps)