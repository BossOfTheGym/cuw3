set(CUW3_LIB_HEADERS
    include/cuw3/arena.hpp
    include/cuw3/assert.hpp
    include/cuw3/atomic.hpp
    include/cuw3/backoff.hpp
    include/cuw3/cuw3.hpp
    include/cuw3/defs.hpp
    include/cuw3/export.hpp
    include/cuw3/fast_arena.hpp
    include/cuw3/funcs.hpp
    include/cuw3/handle.hpp
    include/cuw3/list.hpp
    include/cuw3/pool.hpp
    include/cuw3/ptr.hpp
    include/cuw3/region_allocator.hpp
    include/cuw3/region_pool.hpp
    include/cuw3/retire_reclaim.hpp
    include/cuw3/thread_local_allocator.hpp
    include/cuw3/utils.hpp
    include/cuw3/vmem.hpp
)

set(CUW3_LIB_SOURCES
    src/cuw3.cpp
    src/vmem.cpp
)

if(CUW3_BUILD_SHARED)
    set(CUW3_LIB_TYPE SHARED)
else()
    set(CUW3_LIB_TYPE STATIC)
endif()

configure_file(config.hpp.in include/cuw3/config.hpp @ONLY)

add_library(cuw3 ${CUW3_LIB_TYPE} ${CUW3_LIB_SOURCES})

# TODO : (learn about) target_sources and FILE_SET to install headers
target_include_directories(cuw3
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(CUW3_BUILD_SHARED)
    target_compile_definitions(cuw3 PRIVATE CUW3_EXPORTS)
    target_compile_definitions(cuw3 PUBLIC CUW3_IMPORTS)
endif()


install(
    TARGETS cuw3
    EXPORT cuw3Targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
    EXPORT cuw3Targets
    FILE cuw3Targets.cmake
    NAMESPACE cuw3::
    DESTINATION ${CUW3_INSTALL_CONFIGDIR}
)

install(
    DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/cuw3
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    DIRECTORY include/cuw3
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/cuw3Config.cmake
    INSTALL_DESTINATION ${CUW3_INSTALL_CONFIGDIR}
    NO_SET_AND_CHECK_MACRO
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/cuw3ConfigVersion.cmake
    VERSION ${CUW3_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/cuw3Config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/cuw3ConfigVersion.cmake
    DESTINATION ${CUW3_INSTALL_CONFIGDIR}
)


# TODO : create sort of a utility to make life less painful on windows
#cmake_minimum_required(VERSION 3.30)
#
#project(cuw3_user DESCRIPTION "test cuw3 installation" LANGUAGES C CXX)
#
## provide via command line using -DCMAKE_PREFIX_PATH="<path>;..."
#find_package(cuw3 CONFIG REQUIRED)
#
#add_executable(cuw3_user cuw3_user.cpp)
#target_link_libraries(cuw3_user PRIVATE cuw3::cuw3)
#add_custom_command(
#    TARGET cuw3_user POST_BUILD
#    COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:cuw3_user> $<TARGET_RUNTIME_DLLS:cuw3_user>
#    COMMAND_EXPAND_LISTS
#)