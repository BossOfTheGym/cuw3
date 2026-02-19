enable_testing()


add_executable_ext(dummy dummy.cpp)
target_link_libraries(dummy cuw3)

add_executable_ext(test_vmem_dummy test_vmem_dummy.cpp)
target_link_libraries(test_vmem_dummy cuw3)


add_executable_ext(test_atomic test_atomic.cpp)
target_link_libraries(test_atomic cuw3)

add_executable_ext(test_bitmap test_bitmap.cpp)
target_link_libraries(test_bitmap cuw3)

add_executable_ext(test_fast_arena_allocator test_fast_arena_allocator.cpp)
target_link_libraries(test_fast_arena_allocator cuw3)

add_executable_ext(test_funcs test_funcs.cpp)
target_link_libraries(test_funcs cuw3)

add_executable_ext(test_pool_allocator test_pool_allocator.cpp)
target_link_libraries(test_pool_allocator cuw3)

add_executable_ext(test_region_chunk_allocator test_region_chunk_allocator.cpp)
target_link_libraries(test_region_chunk_allocator cuw3)

add_executable_ext(test_thread_graveyard test_thread_graveyard.cpp)
target_link_libraries(test_thread_graveyard cuw3)

add_executable_ext(test_vmem test_vmem.cpp)
target_link_libraries(test_vmem cuw3)


add_test(NAME dummy COMMAND dummy)