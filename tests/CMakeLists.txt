enable_testing()

add_executable(dummy dummy.cpp)
target_link_libraries(dummy cuw3)

add_test(NAME dummy COMMAND dummy)