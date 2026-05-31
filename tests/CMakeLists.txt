add_executable_ext(cuw3_tests 
    test_atomic.cpp
    test_bitmap.cpp
    test_fast_arena_allocator.cpp
    test_region_chunk_allocator.cpp
    test_thread_graveyard.cpp
    test_vmem.cpp
    test_cuw3.cpp
)

target_link_libraries(cuw3_tests PRIVATE cuw3 gtest gtest_main)

gtest_discover_tests(cuw3_tests)