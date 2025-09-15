cmake_minimum_required(VERSION 3.15)

option(CUW3_BUILD_TESTS "build tests" ON)
option(CUW3_BUILD_EXAMPLES "build examples" ON)
option(CUW3_BUILD_BENCHMARKS "build benchmarks" ON)
option(CUW3_BUILD_SHARED "build as shared library" ON)

set(CUW3_BUILD_CONFIG $<CONFIG>) # TODO : check

set(CUW3_VERSION_MAJOR 0)
set(CUW3_VERSION_MINOR 0)
set(CUW3_VERSION ${CUW3_VERSION_MAJOR}.${CUW3_VERSION_MINOR})

# TODO : clang compiler selection
# TODO : assert & abort definitions
# TODO : consts definitions
# TODO : config definitions
# TODO : set definitions
#if(WIN32)
#	#set(platform "windows")
#elseif(UNIX)
#	#set(platform "unix")
#else()
#    message(FATAL_ERROR "unknown platform")
#endif()

if (MSVC)
	add_compile_options("/Zc:preprocessor") # __VA_OPT__ fix for MSVC
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


project(cuw3 VERSION ${CUW3_VERSION} DESCRIPTION "yet another lock-free memory allocator" LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CUW3_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR}/$<IF:$<CONFIG:Release>,,$<CONFIG>>)
set(CUW3_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR}/$<IF:$<CONFIG:Release>,,$<CONFIG>>)
set(CUW3_INSTALL_DOCDIR ${CMAKE_INSTALL_DOCDIR})
set(CUW3_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
set(CUW3_INSTALL_CONFIGDIR ${CMAKE_INSTALL_LIBDIR}/cmake/cuw3)

function(add_executable_ext target)
    add_executable(${target} ${ARGN})
    add_custom_command(
        TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:${target}> $<TARGET_RUNTIME_DLLS:${target}>
        COMMAND_EXPAND_LISTS
    )
endfunction()

add_subdirectory(cuw3)
if(CUW3_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(CUW3_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(CUW3_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()