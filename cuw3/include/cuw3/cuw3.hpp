#pragma once

#include "export.hpp"

#include "typedefs.hpp"

extern "C" {
    CUW3_API void* cuw3_alloc(uint64_t size, uint64_t alignment);
    CUW3_API void cuw3_free(void* ptr, uint64_t size);
    CUW3_API void cuw3_reclaim();
    CUW3_API void cuw3_cleanup();
}