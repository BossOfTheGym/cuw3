#if defined(_WIN32)
#include <windows.h>
#undef min
#undef max

#pragma comment(lib, "mincore")

#include <algorithm>

#include "cuw3/defs.hpp"
#include "cuw3/vmem.hpp"

namespace cuw3 {
    CUW3_API usize vmem_page_size() {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        return si.dwPageSize; // typically 4KiB
    }

    CUW3_API usize vmem_huge_page_size() {
        return GetLargePageMinimum(); // typically 2MiB
    }

    CUW3_API usize vmem_alloc_granularity() {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        return si.dwAllocationGranularity; // typically 64KiB
    }


    CUW3_API void* vmem_alloc(usize size, VMemAllocType alloc_type) {
        DWORD alloc_flags = {};
        if ((alloc_type & VMemReserveCommit) == VMemReserve) {
            alloc_flags = MEM_RESERVE;
        } else if ((alloc_type & VMemReserveCommit) == VMemReserveCommit) {
            alloc_flags = MEM_RESERVE | MEM_COMMIT;
        } else {
            return nullptr;
        }
        DWORD protection_flags = PAGE_READWRITE;
        return VirtualAlloc(nullptr, size, alloc_flags, protection_flags);
    }

    CUW3_API void* vmem_alloc_aligned(usize size, VMemAllocType alloc_type, usize desired_alignment) {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        usize alignment = std::max<usize>({si.dwPageSize, si.dwAllocationGranularity, desired_alignment});

        DWORD alloc_flags = MEM_RESERVE | (alloc_type & VMemReserveCommit ? MEM_COMMIT : 0);
        DWORD protection_flags = PAGE_READWRITE;

        MEM_ADDRESS_REQUIREMENTS addr_reqs{};
        addr_reqs.Alignment = alignment;

        MEM_EXTENDED_PARAMETER params{};
        params.Type = MemExtendedParameterAddressRequirements;
        params.Pointer = &addr_reqs;

        return VirtualAlloc2(nullptr, nullptr, size, alloc_flags, protection_flags, &params, 1);
    }

    CUW3_API bool vmem_free(void* mem, usize size) {
        return VirtualFree(mem, 0, MEM_RELEASE);
    }


    CUW3_API bool vmem_commit(void* mem, usize size) {
        return VirtualAlloc(mem, size, MEM_COMMIT, PAGE_READWRITE);
    }

    CUW3_API bool vmem_decommit(void* mem, usize size) {
        return VirtualFree(mem, size, MEM_DECOMMIT);
    }

    CUW3_API ErrorCode vmem_get_last_error() {
        return GetLastError();
    }
}
#endif // _WIN32


#if defined(linux)
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>

#include "cuw3/defs.hpp"
#include "cuw3/vmem.hpp"
#include "cuw3/funcs.hpp"

#include <algorithm>

namespace cuw3 {
    CUW3_API usize vmem_page_size() {
        return getpagesize(); // typically 4KiB
    }

    CUW3_API usize vmem_huge_page_size() {
        // TODO : workaround, read this value from /proc/meminfo
        return CUW3_HUGEPAGE_SIZE; // typically 2MiB
    }

    CUW3_API usize vmem_alloc_granularity() {
        return getpagesize(); // typically equals to page size
    }


    CUW3_API void* vmem_alloc(usize size, VMemAllocType alloc_type) {
        int flags = MAP_PRIVATE | MAP_ANONYMOUS;
        int protection = {};
        if ((alloc_type & VMemReserveCommit) == VMemReserve) {
            protection = PROT_NONE;
        } else if ((alloc_type & VMemReserveCommit) == VMemReserveCommit) {
            protection = PROT_READ | PROT_WRITE;
        } else {
            return nullptr;
        }
        void* mem = mmap(nullptr, size, protection, flags, -1, 0);
        // it seems okay to use nullptr as failed value here as we dont use MAP_FIXED to create a new mapping
        // and mmap never returns nullptr as a valid address in such a case
        return mem != MAP_FAILED ? mem : nullptr;
    }

    CUW3_API void* vmem_alloc_aligned(usize size, VMemAllocType alloc_type, usize desired_alignment) {
        usize page_size = vmem_page_size();
        usize alignment = std::max(page_size, desired_alignment);
        usize aligned_size = align(size, alignment);

        if (alignment == page_size) {
            return vmem_alloc(size, alloc_type);
        }

        void* raw_mem = vmem_alloc(aligned_size * 2, VMemReserve);
        if (!raw_mem) {
            return raw_mem;
        }

        void* aligned_mem = align(raw_mem, alignment);

        // tail always exists, head may be empty 
        usize head_size = subptr(aligned_mem, raw_mem);
        if (head_size > 0) {
            vmem_free(raw_mem, head_size);
        }
        vmem_free(advance_ptr(aligned_mem, aligned_size), aligned_size - head_size);

        if (alloc_type == VMemReserveCommit) {
            vmem_commit(aligned_mem, aligned_size);
        }
        return aligned_mem;
    }

    CUW3_API bool vmem_free(void* mem, usize size) {
        return munmap(mem, size) == 0;
    }


    CUW3_API bool vmem_commit(void* mem, usize size) {
        return mprotect(mem, size, PROT_READ | PROT_WRITE) == 0;
    }

    CUW3_API bool vmem_decommit(void* mem, usize size) {
        return mprotect(mem, size, PROT_NONE) == 0;
    }


    CUW3_API ErrorCode vmem_get_last_error() {
        return errno;
    }
}
#endif