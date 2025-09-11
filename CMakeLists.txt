cmake_minimum_required(VERSION 3.15)

option(CUW3_BUILD_TESTS "build tests" ON)
option(CUW3_BUILD_EXAMPLES "build examples" ON)
option(CUW3_BUILD_BENCHMARKS "build benchmarks" ON)

project(cuw3 VERSION 0.0.0.1 DESCRIPTION "yet another lock-free memory allocator" LANGUAGES C CXX)

set(CMAKE_CXX_STANDART 20)
set(CMAKE_CXX_STANDART_REQUIRED True)

# TODO : cuw3

if(CUW3_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(CUW3_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(CUW3_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()