#include "cuw3/conf.hpp"
#include "cuw3/vmem.hpp"
#include "cuw3/region_chunk_allocator.hpp"

#include <queue>
#include <mutex>
#include <atomic>
#include <memory>
#include <vector>
#include <random>
#include <barrier>
#include <variant>
#include <algorithm>
#include <condition_variable>

#include "tests_common.hpp"

using namespace cuw3;

void run_region_specs_tests(const RegionChunkAllocatorSpecs& specs) {
    // search_hosting_region
    for (uint region = 0; region < specs.num_regions; region++) {
        auto& region_specs = specs.region_specs[region];
        if (region_specs.is_empty()) {
            continue;
        }

        uint64 addr_start = region_specs.get_first_offset();
        uint64 addr_end = region_specs.get_last_offset();
        CUW3_CHECK(specs.search_hosting_region(addr_start) == region, "invalid region located");
        CUW3_CHECK(specs.search_hosting_region(addr_start + (addr_end - addr_start) / 2) == region, "invalid region located");
        CUW3_CHECK(specs.search_hosting_region(addr_end - 1) == region, "invalid region located");
    }

    // search_suitable_region
    for (uint region = 0; region < specs.num_regions; region++) {
        auto& region_specs = specs.region_specs[region];
        if (region_specs.is_empty()) {
            continue;
        }

        uint64 size = region_specs.get_chunk_size();
        CUW3_CHECK(specs.search_suitable_region(size) == region, "invalid region to allocate chunk from");
        CUW3_CHECK(specs.search_suitable_region(size - 1) == region, "invalid region to allocate chunk from");
    }

    // locate_chunk
    for (uint region = 0; region < specs.num_regions; region++) {
        auto& region_specs = specs.region_specs[region];
        if (region_specs.is_empty()) {
            continue;
        }
        for (uint32 chunk = 0; chunk < region_specs.num_handles; chunk++) {
            auto handle = chunk + region_specs.handle_offset;
            auto relptr = region_specs.get_relchunk(chunk);

            auto located1 = specs.locate_chunk(relptr);
            CUW3_CHECK(located1.region == region, "invalid region clocated");
            CUW3_CHECK(located1.chunk == chunk, "invalid chunk located");
            CUW3_CHECK(located1.handle == handle, "invalid handle located");

            auto located2 = specs.locate_chunk(relptr + region_specs.get_chunk_size() - 1);
            CUW3_CHECK(located2.region == region, "invalid region clocated");
            CUW3_CHECK(located2.chunk == chunk, "invalid chunk located");
            CUW3_CHECK(located2.handle == handle, "invalid handle located");
        }
    }
}

void test_region_allocator_specs() {
    {
        constexpr uint64 num_regions = conf_max_region_sizes;

        uint64 region_sizes[num_regions] = {5, 6, 7, 8, 9, 10, 11, 12};
        uint64 region_chunk_sizes[num_regions] = {2, 3, 4, 5, 6, 7, 8, 9};

        RegionChunkAllocatorSpecsConfig config{};
        config.region_sizes = region_sizes;
        config.num_region_sizes = num_regions;
        config.region_chunk_sizes = region_chunk_sizes;
        config.num_region_chunk_sizes = num_regions;
        config.handle_size = conf_region_handle_size;
        config.region_alignment = 8;
        config.handle_alignment = 8;

        RegionChunkAllocatorSpecs specs{};
        auto* check = RegionChunkAllocatorSpecs::create(Memory::from(&specs), config);
        CUW3_CHECK(check, "failed to create region specs");

        run_region_specs_tests(specs);
    }

    {
        constexpr uint64 num_regions = conf_max_region_sizes;

        uint64 region_sizes[num_regions] = {10, 10, 10, 10, 10, 10, 10, 10};
        uint64 region_chunk_sizes[num_regions] = {2, 3, 4, 5, 6, 7, 8, 9};

        RegionChunkAllocatorSpecsConfig config{};
        config.region_sizes = region_sizes;
        config.num_region_sizes = num_regions;
        config.region_chunk_sizes = region_chunk_sizes;
        config.num_region_chunk_sizes = num_regions;
        config.handle_size = conf_region_handle_size;
        config.region_alignment = 8;
        config.handle_alignment = 8;

        RegionChunkAllocatorSpecs specs{};
        auto* check = RegionChunkAllocatorSpecs::create(Memory::from(&specs), config);
        CUW3_CHECK(check, "failed to create region specs");

        run_region_specs_tests(specs);
    }
}


void test_region_allocator_pools() {
    constexpr uint64 num_regions = conf_max_region_sizes;

    uint64 region_sizes[num_regions] = {5, 6, 7, 8, 9, 10, 11, 12};
    uint64 region_chunk_sizes[num_regions] = {2, 3, 4, 5, 6, 7, 8, 9};

    RegionChunkAllocatorSpecsConfig specs_config{};
    specs_config.region_sizes = region_sizes;
    specs_config.num_region_sizes = num_regions;
    specs_config.region_chunk_sizes = region_chunk_sizes;
    specs_config.num_region_chunk_sizes = num_regions;
    specs_config.handle_size = conf_region_handle_size;
    specs_config.region_alignment = 8;
    specs_config.handle_alignment = 8;

    RegionChunkAllocatorSpecs specs{};
    auto* specs_config_check = RegionChunkAllocatorSpecs::create(Memory::from(&specs), specs_config);
    CUW3_CHECK(specs_config_check, "failed to create region specs");

    constexpr uint64 contention_split = 8;

    RegionChunkAllocatorPoolsConfig pools_config{};
    pools_config.specs = &specs;
    pools_config.contention_split = contention_split;

    RegionChunkAllocatorPools pools{};
    auto* pools_check = RegionChunkAllocatorPools::create(Memory::from(&pools), pools_config);
    CUW3_CHECK(pools_check, "failed to create allocator pools");

    for (uint32 region = 0; region < pools.num_regions; region++) {
        for (uint32 split = 0; split < pools.num_splits; split++) {
            auto& pool_entry = pools.pool_entries[region][split];
            for (uint32 handle = pool_entry.first_handle; handle < pool_entry.last_handle; handle++) {
                uint32 result = pools.search_pool_split(region, handle);
                CUW3_CHECK(result == split, "invalid split located");
            }    
        }
    }
}

struct TestRegionChunkAllocator {
    TestRegionChunkAllocator() {
        constexpr uint32 num_regions = 8;

        uint64 region_sizes[num_regions] = {13, 14, 15, 16, 17, 18, 19, 20};
        uint64 region_chunk_sizes[num_regions] = {2, 3, 4, 5, 6, 7, 8, 9};

        RegionChunkAllocatorSpecsConfig specs_config{};
        specs_config.region_sizes = region_sizes;
        specs_config.num_region_sizes = num_regions;
        specs_config.region_chunk_sizes = region_chunk_sizes;
        specs_config.num_region_chunk_sizes = num_regions;
        specs_config.handle_size = conf_region_handle_size;
        specs_config.region_alignment = 8;
        specs_config.handle_alignment = 8;

        auto* specs_config_check = RegionChunkAllocatorSpecs::create(Memory::from(&specs), specs_config);
        CUW3_CHECK(specs_config_check, "failed to create region specs");


        constexpr uint64 contention_split = 8;

        RegionChunkAllocatorPoolsConfig pools_config{};
        pools_config.specs = &specs;
        pools_config.contention_split = contention_split;

        auto* pools_check = RegionChunkAllocatorPools::create(Memory::from(&pools), pools_config);
        CUW3_CHECK(pools_check, "failed to create allocator pools");


        regions = VMemPtr::create(specs.total_regions_size);
        handles = VMemPtr::create(specs.total_handles_size);
        pool_handles = VMemPtr::create(specs.total_pool_handles_size);
        CUW3_CHECK(regions, "failed to allocate regions");
        CUW3_CHECK(handles, "failed to allocate handles");
        CUW3_CHECK(pool_handles, "failed to allocate pool_handles");


        RegionAllocatorConfig allocator_config{};
        allocator_config.specs = &specs;
        allocator_config.pools = &pools;
        allocator_config.regions = regions.ptr();
        allocator_config.handles = handles.ptr();
        allocator_config.pool_handles = pool_handles.ptr();

        auto* allocator_check = RegionChunkAllocator::create(Memory::from(&allocator), allocator_config);
        CUW3_CHECK(allocator_check, "failed to create an allocator");
    }

    [[nodiscard]] RegionChunkAllocation allocate_chunk(uint32 region, uint32 split_start) {
        RegionChunkAllocParams alloc_params{};
        alloc_params.rounds = 4;
        alloc_params.attempts = 4;
        alloc_params.split_start = split_start;
        return allocator.allocate_chunk(region, alloc_params);
    }

    void deallocate_chunk(RegionChunkAllocation allocation) {
        allocator.deallocate_chunk(allocation);
    }

    void deallocate_chunk(RegionChunkMemory memory) {
        allocator.deallocate_chunk(memory);
    }

    uint32 get_num_regions() const {
        return specs.num_regions;
    }

    RegionChunkAllocatorSpecs specs{};
    RegionChunkAllocatorPools pools{};
    VMemPtr regions{};
    VMemPtr handles{};
    VMemPtr pool_handles{};
    RegionChunkAllocator allocator{};
};

template<class T>
struct TestRegionChunkAllocatorCache {
    T& operator[] (uint index) {
        CUW3_CHECK(index < conf_num_regions, "too big for region index");

        return data[index];
    }

    T data[conf_num_regions] = {};
};

// TODO : test all alloc
// TODO : test allocated amount sums up
// TODO : test all dealloc
void test_region_allocator_st(uint rounds) {
    TestRegionChunkAllocator allocator{};

    TestRegionChunkAllocatorCache<uint32> split_cache{};
    TestRegionChunkAllocatorCache<std::vector<RegionChunkAllocation>> allocations_cache{};

    for (uint round = 0; round < rounds; round++) {
        for (uint region = 0; region < allocator.get_num_regions(); region++) {
            while (true) {
                auto allocation = allocator.allocate_chunk(region, split_cache[region]);
                if (!allocation) {
                    break;
                }
                split_cache[region] = allocation.split;
                allocations_cache[region].push_back(allocation);
            }
        }
        for (uint region = 0; region < allocator.get_num_regions(); region++) {
            CUW3_CHECK(allocations_cache[region].size() == allocator.specs.region_specs[region].num_handles, "failed to poperly exhaust region pool");

            shuffle(allocations_cache[region]);
            for (auto allocation : allocations_cache[region]) {
                allocator.deallocate_chunk(allocation);
            }
            allocations_cache[region].clear();
        }
    }
}

void test_region_allocator_mt(uint threads, uint rounds) {
    TestRegionChunkAllocator allocator{};

    struct ThreadCtx {
        void clear() {
            for (auto& split : split_cache.data) {
                split = 0;
            }
            for (auto& cache : allocations_cache.data) {
                cache.clear();
            }
        }

        TestRegionChunkAllocatorCache<uint32> split_cache{};
        TestRegionChunkAllocatorCache<std::vector<RegionChunkAllocation>> allocations_cache{};
    };

    std::vector<ThreadCtx> thread_contexts(threads);

    auto check_all_allocated = [&] () {
        TestRegionChunkAllocatorCache<uint32> total{};
        for (auto& thread_ctx : thread_contexts) {
            for (uint32 region = 0; region < allocator.get_num_regions(); region++) {
                total[region] += thread_ctx.allocations_cache[region].size();
            }
        }
        for (uint32 region = 0; region < allocator.get_num_regions(); region++) {
            CUW3_CHECK(total[region] == allocator.specs.region_specs[region].num_handles, "failed to allocate all chunks");
        }
    };

    std::barrier all_allocated{threads, check_all_allocated};
    std::barrier all_deallocated{threads};

    std::vector<std::thread> workers(threads);
    for (uint thread_id = 0; thread_id < threads; thread_id++) {
        workers[thread_id] = std::thread([&, thread_id, rounds](){
            auto& thread_ctx = thread_contexts[thread_id];

            for (uint round = 0; round < rounds; round++) {
                thread_ctx.clear();
    
                all_deallocated.arrive_and_wait();
    
                for (uint32 region = 0; region < allocator.get_num_regions(); region++) {
                    while (true) {
                        auto allocation = allocator.allocate_chunk(region, thread_ctx.split_cache[region]);
                        if (!allocation) {
                            break;
                        }
                        thread_ctx.split_cache[region] = allocation.split;
                        thread_ctx.allocations_cache[region].push_back(allocation);
                    }
                }
    
                all_allocated.arrive_and_wait();
    
                for (uint32 region = 0; region < allocator.get_num_regions(); region++) {
                    for (auto allocation : thread_ctx.allocations_cache[region]) {
                        allocator.deallocate_chunk(allocation);
                    }
                }
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
}


int main() {
    test_region_allocator_specs();
    test_region_allocator_pools();
    test_region_allocator_st(16);
    test_region_allocator_mt(8, 64);

    std::cout << "done!" << std::endl;
    return 0;
}