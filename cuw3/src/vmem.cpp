#ifdef _WIN32
#include <windows.h>
#undef min
#undef max

#pragma comment(lib, "mincore")
#endif

#include <algorithm>

#include "cuw3/vmem.hpp"

namespace cuw3 {
#ifdef _WIN32
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
        DWORD alloc_flags = MEM_RESERVE | (alloc_type == VMemReserveCommit ? MEM_COMMIT : 0);
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
#endif // _WIN32
}