enable_testing()

add_executable_ext(dummy dummy.cpp)
target_link_libraries(dummy cuw3)

add_executable_ext(test_vmem_dummy test_vmem_dummy.cpp)
target_link_libraries(test_vmem_dummy cuw3)

add_test(NAME dummy COMMAND dummy)