#pragma once

#include <cstdint>
#include <cstddef>
#include <concepts>
#include <type_traits>

#include "cuw3/config.hpp" // generated
#include "cuw3/typedefs.hpp"

#define CUW3_CACHELINE_SIZE 64
#define CUW3_CONTROL_BLOCK_SIZE 128

#define CUW3_PAGE_SIZE 4096
#define CUW3_HUGEPAGE_SIZE (1 << 21)

#define CUW3_MAX_REGION_SIZES 8
#define CUW3_REGION_SIZES_LOG2       36,36,36,36,36,36
#define CUW3_REGION_CHUNK_SIZES_LOG2 21,22,23,24,25,26

#define CUW3_REGION_CHUNK_POOL_CONTENTION_SPLIT 2

#define CUW3_MAX_CONTENTION_SPLIT 16

#define CUW3_MIN_ALLOC_SIZE 16
#define CUW3_MIN_ALLOC_ALIGNMENT 16

#define CUW3_GRAVEYARD_SLOT_COUNT 16

// TODO : platform defines
// TODO : build configuration defines