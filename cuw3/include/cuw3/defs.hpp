#pragma once

#include <cstdint>
#include <cstddef>
#include <concepts>
#include <type_traits>

#include "cuw3/config.hpp" // generated, must be included using "cuw3/..."
#include "typedefs.hpp"


// macros definitions are unchecked, all checks are done within conf.hpp
#define CUW3_CACHELINE_SIZE 64
#define CUW3_CONTROL_BLOCK_SIZE 128

#define CUW3_NEW_CACHELINE alignas(CUW3_CACHELINE_SIZE)

#define CUW3_PAGE_SIZE 4096
#define CUW3_HUGEPAGE_SIZE (1 << 21)

#define CUW3_MAX_REGION_SIZES 8
#define CUW3_REGION_SIZES_LOG2       36,36,36,36,36,36
#define CUW3_REGION_CHUNK_SIZES_LOG2 21,22,23,24,25,26

// simplified check, if chunk size is less than or equal to this value then we can cache it
#define CUW3_MAX_CACHED_CHUNK_SIZE_ID 5

#define CUW3_REGION_CHUNK_POOL_CONTENTION_SPLIT 2

#define CUW3_MAX_CONTENTION_SPLIT 16

#define CUW3_MIN_ALLOC_SIZE 16
#define CUW3_MIN_ALLOC_ALIGNMENT 16

#define CUW3_GRAVEYARD_SLOT_COUNT 16

#define CUW3_MIN_CHUNK_LOG2 4
#define CUW3_MAX_CHUNK_LOG2 13

#define CUW3_MIN_ALIGNMENT_LOG2 4
#define CUW3_MAX_ALIGNMENT_LOG2 12

#define CUW3_MAX_FAST_ARENA_LOOKUP_SPLIT 128
#define CUW3_MAX_FAST_ARENA_LOOKUP_STEPS 11

#define CUW3_SIZE_CUTOFF (1 << 14)


#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
    #define CUW3_ASAN_ENABLED
    #include <sanitizer/asan_interface.h>

    #define CUW3_POISON_MEMORY_REGION(addr, size) ASAN_POISON_MEMORY_REGION((addr),(size))
    #define CUW3_UNPOISON_MEMORY_REGION(addr, size) ASAN_UNPOISON_MEMORY_REGION((addr), (size))
#else
    #define CUW3_POISON_MEMORY_REGION(addr, size) ((void)(addr), (void)(size))
    #define CUW3_UNPOISON_MEMORY_REGION(addr, size) ((void)(addr), (void)(size))
#endif

