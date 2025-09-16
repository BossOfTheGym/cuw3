#pragma once

#include "defs.hpp"
#include "export.hpp"

// this is rather limited abstraction of the virtual memory system
// memory access rights are always read-write
// memory can be reserved or reserved and committed only
// memory can be committed/decommitted later
namespace cuw3 {
    enum VMemAllocType : uintptr {
        VMemReserve = 1,
        VMemCommit = 2,
        VMemReserveCommit = VMemReserve | VMemCommit,
        VMemHugepages = 4,
    };

    // TODO : or just leave it as uint64
    using ErrorCode = uint64;

    CUW3_API usize vmem_page_size();
    CUW3_API usize vmem_huge_page_size();
    CUW3_API usize vmem_alloc_granularity();

    CUW3_API void* vmem_alloc(usize size, VMemAllocType alloc_type);
    CUW3_API void* vmem_alloc_aligned(usize size, VMemAllocType alloc_type, usize desired_alignment);
    CUW3_API bool vmem_free(void* mem, usize size);

    CUW3_API bool vmem_commit(void* mem, usize size);
    CUW3_API bool vmem_decommit(void* mem, usize size);

    CUW3_API ErrorCode vmem_get_last_error();
}