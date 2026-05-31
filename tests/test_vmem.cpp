#include <cstring>
#include <iostream>

#include "cuw3/vmem.hpp"
#include "cuw3/utils.hpp"
#include "cuw3/assert.hpp"

#include <gtest/gtest.h>

using namespace cuw3;

void test_vmem_page_sizes() {
    CUW3_CHECK(vmem_page_size() > 0, "page_size must be positive");
    CUW3_CHECK(vmem_alloc_granularity() > 0, "allocation granularity must be positive");
    CUW3_CHECK(is_alignment(vmem_page_size()), "page_size must be a power of 2");
    CUW3_CHECK(is_alignment(vmem_alloc_granularity()), "allocation granularity must be a power of 2");
}

void test_vmem_alloc_free() {
    void* alloc = vmem_alloc(1 << 20, VMemAllocType::VMemReserveCommit);
    CUW3_CHECK(alloc, "vmem_alloc failed");
    vmem_free(alloc, 1 << 20);
}

void test_vmem_alloc_aligned() {
    void* alloc_aligned = vmem_alloc_aligned(1 << 23, VMemAllocType::VMemReserveCommit, 1 << 22);
    CUW3_CHECK(alloc_aligned, "vmem_alloc_aligned failed");
    CUW3_CHECK(is_aligned(alloc_aligned, 1 << 22), "allocation must be aligned");
    vmem_free(alloc_aligned, 1 << 23);
}

void test_vmem_reserve_commit_decommit() {
    void* alloc_reserved = vmem_alloc(1 << 20, VMemAllocType::VMemReserve);
    CUW3_CHECK(alloc_reserved, "vmem_alloc with VMemReserve failed");
    CUW3_CHECK(vmem_commit(alloc_reserved, 1 << 20), "vmem_commit failed");
    CUW3_CHECK(vmem_decommit(alloc_reserved, 1 << 20), "vmem_decommit failed");
    vmem_free(alloc_reserved, 1 << 20);
}

TEST(VMem, PageSizes) {
    test_vmem_page_sizes();
}

TEST(VMem, AllocFree) {
    test_vmem_alloc_free();
}

TEST(VMem, AllocAligned) {
    test_vmem_alloc_aligned();
}

TEST(VMem, ReserveCommitDecommit) {
    test_vmem_reserve_commit_decommit();
}
