add_executable_ext(benchmarks benchmarks.cpp)
target_link_libraries(benchmarks cuw3 benchmark::benchmark benchmark::benchmark_main)