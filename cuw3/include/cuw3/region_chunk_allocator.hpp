#pragma once

#include <array>
#include <cstring>
#include <algorithm>
#include <functional>

#include "ptr.hpp"
#include "conf.hpp"
#include "vmem.hpp"
#include "atomic.hpp"
#include "backoff.hpp"

namespace cuw3 {
    /* example implementation where everything can be calculated, almost no memory accesses are required */
    /*
    struct ExampleRegionLocatorConsts {
        static constexpr ExampleRegionLocatorConsts create(uint64 _num_regions, uint64 _region_size, uint64 _handle_size, uint64 _min_subregion_size) {
            ExampleRegionLocatorConsts consts{};
            consts.num_regions = _num_regions;

            consts.region_size = _region_size;
            consts.region_size_log2 = intlog2(consts.region_size);

            consts.handle_size = _handle_size;
            consts.handle_size_log2 = intlog2(consts.handle_size_log2);

            consts.min_subregion_size_log2 = intlog2(_min_subregion_size);

            // assert region_size_log2 >= min_subregion_size_log2 + num_regions - 1
            consts.sizes_mask_length = consts.region_size_log2 - consts.min_subregion_size_log2 + 1;
            consts.sizes_mask = (((uint64)1 << consts.num_regions) - 1) << (consts.sizes_mask_length - consts.num_regions);
    
            return consts;
        }

        uint64 num_regions{};

        uint64 region_size{};
        uint64 region_size_log2{};

        uint64 handle_size{};
        uint64 handle_size_log2{};

        uint64 min_subregion_size_log2{};

        uint64 sizes_mask_length{};
        uint64 sizes_mask{};
    };

    struct ExampleRegionLocation {
        uint64 region{};
        uint64 handle{};
    };

    struct ExampleRegionLocator {
        ExampleRegionLocation locate(uint64 ptr) {
            constexpr uint64 all_mask = ~(uint64)0;

            uint64 diff = ptr - regions;
            uint64 region_num = divpow2(diff, consts.region_size_log2);
            uint64 handle_base = consts.sizes_mask & (all_mask << (consts.sizes_mask_length - region_num));
            uint64 handle_num = handle_base + modpow2(diff, consts.min_subregion_size_log2 + region_num);
            return {region_num, handle_num};
        }

        ExampleRegionLocatorConsts consts{};
        uint64 regions{};
        uint64 handles{};
    };
    */

    /* code to test code above */
    /*
    constexpr auto consts = cuw3::ExampleRegionLocatorConsts::create(4, 1 << 8, 1, 1 << 5);

    std::cout << "num_regions: " << consts.num_regions << std::endl;
    std::cout << "region_size: " << consts.region_size << std::endl;
    std::cout << "region_size_log2: " << consts.region_size_log2 << std::endl;
    std::cout << "handle_size: " << consts.handle_size << std::endl;
    std::cout << "handle_size_log2: " << consts.handle_size_log2 << std::endl;
    std::cout << "min_subregion_size_log2: " << consts.min_subregion_size_log2 << std::endl;
    std::cout << "sizes_mask: " << consts.sizes_mask << std::endl;
    std::cout << "sizes_mask_length: " << consts.sizes_mask_length << std::endl;

    std::cout << std::endl;

    cuw3::ExampleRegionLocation location{};
    cuw3::ExampleRegionLocator locator{consts, 0, 0};
    for (cuw3::uint64 i = 0; i < consts.num_regions; i++) {
        location = locator.locate(i * consts.region_size);
        std::cout << "region: " << location.region << " handle: " << location.handle << std::endl;
    }
    */


    struct RegionSpec {
        uint64 region_offset{}; // byte offset of the region start
        uint64 region_size{}; // byte size of the region
        uint64 chunk_size_log2{}; // intlog2(region_chunk_size)
        uint32 handle_offset{}; // handle offset (index) of the first handle
        uint32 num_handles{}; // number of handles (or chunks) within a region
    };
    
    // NOTE : this is, I think, the most simple version of what can be done
    // Of course, this implementation can be made more adaptable to real-world circumstances
    // Instead of preallocated range of reserved virtual memory we can have several slots (pointers)  
    // Like, one slot points to some allocation of predifined amount of space. We add another one I we truely nned it.
    // This may help to save some virtual emory space as ... it may become an issue with this allocator 
    //
    // stores layout of the regions and handles
    // this is a read-only data structure
    struct RegionAllocatorSpecs {
        // not an error if some region has zero chunks (region size is less than size of a single chunk)
        static bool init(
            RegionAllocatorSpecs* specs,
            const uint64 region_sizes[], uint64 num_region_sizes,
            const uint64 region_chunk_sizes[], uint64 num_region_chunk_sizes,
            uint64 handle_size,
            uint64 region_alignment,
            uint64 handle_alignment
        ) {
            *specs = {};

            if (num_region_sizes == 0 || num_region_sizes > conf_max_region_sizes) {
                return false; 
            }
            if (num_region_chunk_sizes == 0 || num_region_chunk_sizes > conf_max_region_sizes) {
                return false;
            }
            if (num_region_sizes != num_region_chunk_sizes) {
                return false;
            }

            CUW3_ASSERT(specs, "specs is null");
            CUW3_ASSERT(is_pow2(handle_size), "handle size is not power of 2");
            CUW3_ASSERT(is_alignment(region_alignment), "region_alignment is not valid alignment");
            CUW3_ASSERT(is_alignment(handle_alignment), "handle_alignment is not valid alignment");

            bool all_region_sizes_equal = all_equal(region_sizes, region_sizes + num_region_sizes);
            uint64 region_size = all_region_sizes_equal ? align(intpow2(region_sizes[0]), region_alignment) : 0;
            uint64 region_size_log2 = all_region_sizes_equal ? pow2log2(region_size) : 0;

            specs->region_alignment = region_alignment;
            specs->region_size = region_size;
            specs->region_size_log2 = region_size_log2;
            specs->num_regions = num_region_sizes;

            specs->handle_alignment = handle_alignment;
            specs->handle_size = handle_size;
            specs->handle_size_log2 = pow2log2(handle_size);

            uint32 handle_offset = 0;
            uint64 region_offset = 0;
            for (usize i = 0; i < num_region_sizes; i++) {
                uint64 region_size = align(intpow2(region_sizes[i]), region_alignment);
                uint64 chunk_size_log2 = region_chunk_sizes[i];
                uint32 num_handles = divpow2(region_size, chunk_size_log2);

                specs->region_specs[i] = {
                    .region_offset = region_offset,
                    .region_size = region_size,
                    .chunk_size_log2 = chunk_size_log2,
                    .handle_offset = handle_offset,
                    .num_handles = num_handles,
                };

                specs->chunk_size_search_sentinels[i] = intpow2(chunk_size_log2); 
                specs->region_search_sentinels[i] = region_offset + region_size;

                handle_offset += num_handles;
                region_offset += region_size;
            }

            specs->total_regions_size = region_offset;
            specs->total_handles_size = align(mulpow2(handle_offset, specs->handle_size_log2), handle_alignment);
            specs->num_handles = handle_size;

            return true;
        }

        // TODO : move searches here
        RegionSpec region_specs[conf_max_region_sizes] = {};
        
        // NOTE: AVX can be used here to facilitate searching
        uint64 chunk_size_search_sentinels[conf_max_region_sizes] = {};

        // NOTE: AVX can be used here to facilitate searching
        // NOTE: we would like to make this as fast as possible because we call this function on each deallocation
        uint64 region_search_sentinels[conf_max_region_sizes] = {};

        uint64 total_regions_size{}; // in bytes
        uint64 region_alignment{};
        uint64 region_size{}; // in bytes, in case all regions are equal otherwise zero
        uint64 region_size_log2{}; // intlog2(region_size) in case all regions are equal otherwise zero
        uint64 num_regions{};
        
        uint64 total_handles_size{}; // in bytes
        uint64 handle_alignment{};
        uint64 handle_size{}; // in bytes
        uint64 handle_size_log2{};
        uint64 num_handles{};
    };


    using RegionChunkPoolLinkType = uint32;

    inline constexpr RegionChunkPoolLinkType region_chunk_pool_null_link = 0xFFFFFFFF;
    inline constexpr RegionChunkPoolLinkType region_chunk_pool_failed_alloc = 0xFFFFFFFE;

    struct RegionChunkPoolListHead {
        RegionChunkPoolLinkType version{};
        RegionChunkPoolLinkType next{};
    };

    struct RegionPoolCommonTraits {
        using LinkType = RegionChunkPoolLinkType;
        using ListHead = RegionChunkPoolListHead;

        static constexpr LinkType null_link = region_chunk_pool_null_link;
        static constexpr LinkType op_failed = region_chunk_pool_failed_alloc;
    };

    using RegionChunkPoolListView = AtomicListView<RegionPoolCommonTraits>;
    using RegionChunkPoolStackTop = RegionChunkPoolLinkType;
    using RegionChunkPoolStackView = AtomicBumpStackView<RegionPoolCommonTraits>;

    struct alignas(conf_cacheline) RegionPoolEntry {
        RegionChunkPoolListHead free_list{}; // atomic memory lcoation
        RegionChunkPoolStackTop free_stack{}; // atomic memory location
        RegionChunkPoolLinkType stack_limit{}; // readonly memory location
    };

    struct RegionAllocatorPools {
        static bool init(RegionAllocatorPools* pools, const RegionAllocatorSpecs& specs, uint64 contention_split) {
            *pools = {};

            if (contention_split > conf_max_contention_split) {
                return false;
            }

            // this will initialize all not used entries as well
            // all unused entries will be empty: valid state (empty list, empty stack) but nothing to allocate
            for (usize pool = 0; pool < conf_max_region_sizes; pool++) {
                auto& region_specs = specs.region_specs[pool];

                uint32 handles_per_split = (region_specs.num_handles + contention_split - 1) / contention_split;
                for (usize split = 0; split < conf_max_contention_split; split++) {
                    uint64 first_handle = region_specs.handle_offset + std::min<uint32>(handles_per_split * split, region_specs.num_handles);
                    uint64 last_handle = region_specs.handle_offset + std::min<uint32>(handles_per_split * (split + 1), region_specs.num_handles);

                    auto& pool_entry = pools->pool_entries[pool][split];
                    pool_entry.free_list = {0, region_chunk_pool_null_link};
                    pool_entry.free_stack = first_handle;
                    pool_entry.stack_limit = last_handle;

                    pools->split_search_sentinels[pool][split] = last_handle;
                }
            }
            return true;
        }

        // TODO : move searches here

        // NOTE : this layout is not ideal and subject to experimentation and future changes
        // while it is originally aimed to split contention between several thread it is truely unknown how effective it will be
        // Possible experimentation points can be:
        // * store free_stack and stack_limit separately from free_list
        // * free_stack can be sharded separately from free_lists
        RegionPoolEntry pool_entries[conf_max_region_sizes][conf_max_contention_split] = {};

        // in fact, it is enough to search using stack_limit from pool entry
        // but it is too volatile, its cacheline can be modified by multiple threads
        // so it is better to store all sentinels outside of the pool entry
        uint64 split_search_sentinels[conf_max_region_sizes][conf_max_contention_split] = {}; // readonly
    };


    struct RegionChunkLocation {
        explicit operator bool() const {
            return valid;
        }

        uint32 region{}; // region number
        uint32 chunk{}; // chunk index within the region
        uint32 handle{}; // global index of the handle
        uint32 valid{};
    };

    struct RegionChunkAllocation {
        explicit operator bool() const {
            return chunk; // both either null or not
        }

        void* chunk{};
        void* handle{};
    };

    // *** implementation forethought ***
    // data bits representation changes:
    // * when chunk is not allocated then data bits are not used, pointer bits store next pointer
    // * when chunk is allocated we can effeectively store some pointer
    //   * how convinient, we can store thread local pointer in ptr section here and allocator type used for the chunk in the data section
    /*

    https://en.cppreference.com/w/cpp/language/multithread.html

    Data races

    Different threads of execution are always allowed to access (read and modify) different memory locations concurrently, with no interference and no synchronization requirements.

    Two expression evaluations conflict if one of them modifies a memory location or starts/ends the lifetime of an object in a memory location, and the other one reads or modifies the same memory location or starts/ends the lifetime of an object occupying storage that overlaps with the memory location.

    A program that has two conflicting evaluations has a data race unless

    both evaluations execute on the same thread or in the same signal handler, or
    both conflicting evaluations are atomic operations (see std::atomic), or
    one of the conflicting evaluations happens-before another (see std::memory_order). 

    If a data race occurs, the behavior of the program is undefined. 

    */
    // *** multithreading notes ***
    // 1. all accesses to this field must be atomic or else it will be UB (almost all)
    // 2. all accesses to this field from list modification context MUST be atomic (or else it's UB)
    // 3. after we have allocated chunk (it means we either acquired it from the free_stack or from the free_list)
    //    we can atomically modify state of header data field (there can still be threads attempting to read its value though it is totally fine if the read some garbage)
    // 4. after that (if we dont need any modification done to this field) we can read this value non-atomically
    // 5. whenever we need to modify its state we do this, again, atomically like we did in 3.
    // 6. => when we want to return chunk back to list we gotta resort to atomic modifications
    //
    // This must comply with Data races definition from C++ (no data races here).
    //
    // Yeah, setting fixed amount of bits imposes alignment constraints on thread local storage.
    // But it must not be an issue because it will be allocated via virtual memory facility anyway (and common page size is 4Kib anyway (12 zero bits))
    //
    // As the name suggests: this must be placed at the beginning of the handle memory location
    inline constexpr uint64 region_chunk_handle_header_data_bits = 12;
    inline constexpr uint64 region_chunk_handle_header_ptr_alignment = 1 << region_chunk_handle_header_data_bits;

    using RegionChunkHandleHeaderData = AlignmentPackedPtr<uint64, region_chunk_handle_header_data_bits>;

    // this struct must be at the beginning of each handle
    struct RegionChunkHandleHeader {
        RegionChunkHandleHeaderData data{}; // atomic memory location/readonly memory location
    };

    // NOTE : we can add here cache that will store some retired region chunks
    //   so we can fastly store them there without decommitting memory
    //   and we can fastly acquire them from there without committing memory
    // NOTE : we can commit them not all at once but sequentially when needed
    // this is commonly a global shared entity
    struct RegionAllocator {
        static constexpr uint64 min_possible_handle_size = 16;
        static constexpr int chunk_pool_alloc_attempts = 2;
        static constexpr int chunk_alloc_attempts = 2;

        using Backoff = SimpleBackoff;

        // huge_page_size == 0 => huge pages are disabled
        static bool init(
            RegionAllocator* allocator,
            const uint64 region_sizes[],
            uint64 num_region_sizes,
            const uint64 region_chunk_sizes[],
            uint64 num_region_chunk_sizes,
            uint64 page_size,
            uint64 huge_page_size,
            uint64 handle_size,
            uint64 contention_split
        ) {
            *allocator = {};

            if (huge_page_size != 0 && huge_page_size >= page_size) {
                return false; // notify: "invalid value for huge page size"
            }
            if (handle_size < min_possible_handle_size) {
                return false; // notify: "too small value of handle_size"
            }

            uint64 region_alignment = huge_page_size != 0 ? huge_page_size : page_size;
            uint64 handle_alignment = page_size;
            if (!RegionAllocatorSpecs::init(&allocator->specs,
                region_sizes, num_region_sizes,
                region_chunk_sizes, num_region_chunk_sizes,
                handle_size, region_alignment, handle_alignment)) {
                return false;
            }

            if (!RegionAllocatorPools::init(&allocator->pools, allocator->specs, contention_split)) {
                return false;
            }

            allocator->page_size = page_size;
            allocator->huge_page_size = huge_page_size;
            allocator->contention_split = contention_split;

            allocator->regions = vmem_alloc_aligned(allocator->specs.total_regions_size, VMemReserve, allocator->specs.region_alignment);
            allocator->handles = vmem_alloc_aligned(allocator->specs.total_handles_size, VMemReserveCommit, allocator->specs.handle_alignment);

            if (!allocator->regions || !allocator->handles) {
                // here something goes completely wrong: we failed to allocate memory for either regions or handles
                // form this point on we can either crash everything via abort ... or
                // ... or pretend nothing happened and keep returning nullptr on memory allocation requests
                if (allocator->regions) {
                    vmem_free(allocator->regions, allocator->specs.total_regions_size);
                    allocator->regions = nullptr;
                }
                if (allocator->handles) {
                    vmem_free(allocator->handles, allocator->specs.total_handles_size);
                    allocator->handles = nullptr;
                }
                return false;
            }
            return true;
        }

        ~RegionAllocator() {
            if (regions) {
                vmem_free(regions, specs.total_regions_size);
            }
            if (handles) {
                vmem_free(handles, specs.total_handles_size);
            }
        }


        RegionChunkLocation locate_chunk_common(uint64 rel_to_regions, uint32 region) {
            auto& region_specs = specs.region_specs[region];
            CUW3_CHECK(region_specs.num_handles != 0, "we attempted to locate chunk within an empty region.");

            uint64 rel_to_region = rel_to_regions - region_specs.region_offset;
            uint32 chunk = divpow2(rel_to_region, region_specs.chunk_size_log2);
            uint32 handle = region_specs.handle_offset + chunk;
            return {region, chunk, handle, true};
        }

        RegionChunkLocation locate_chunk_all_regions_equal(void* ptr) {
            CUW3_CHECK(specs.region_size != 0, "regions are not equal");
            CUW3_CHECK(belongs_any_region(ptr), "ptr is out if range");

            uint64 rel_to_regions = subptr(ptr, regions);
            uint32 region = divpow2(rel_to_regions, specs.region_size_log2);
            return locate_chunk_common(rel_to_regions, region);
        }

        RegionChunkLocation locate_chunk_all_regions_differ(void* ptr) {
            CUW3_ASSERT(specs.region_size == 0, "all regions are equal: more effective algorithm available.");
            CUW3_CHECK(belongs_any_region(ptr), "ptr is out of range");

            auto search_hosting_region = [&] (uint64 relptr) -> uint32 {
                for (uint i = 0; i < specs.num_regions; i++) {
                    if (relptr < specs.region_search_sentinels[i]) {
                        return i;
                    }
                }
                return last_region();
            };
            
            uint64 rel_to_regions = subptr(ptr, regions);
            uint32 region = search_hosting_region(rel_to_regions);
            CUW3_CHECK(region != last_region(), "unreachable reached: sentinel search exausted");
            return locate_chunk_common(rel_to_regions, region);
        }


        struct ExternalDataOps {
            RegionChunkHandleHeader* get_handle(RegionChunkPoolLinkType node) {
                return (RegionChunkHandleHeader*)advance_arr_log2(allocator->handles, allocator->specs.handle_size_log2, node);
            }

            // exclusive modification here while we attempt to push it back into the list
            void set_next(RegionChunkPoolLinkType node, RegionChunkPoolLinkType next) {
                CUW3_ASSERT(node < allocator->specs.num_handles, "invalid link value 'node' passed");
                CUW3_ASSERT(next < allocator->specs.num_handles || next == region_chunk_pool_null_link, "invalid link value 'next' passed");

                auto new_data = RegionChunkHandleHeaderData::packed_shifted(next, 0);
                auto* handle = get_handle(node);
                std::atomic_ref{handle->data}.store(new_data, std::memory_order_relaxed);
            }

            RegionChunkPoolLinkType get_next(RegionChunkPoolLinkType node) {
                CUW3_ASSERT(node < allocator->specs.num_handles, "invalid node handle passed");

                auto* handle = get_handle(node);
                return std::atomic_ref{handle->data}
                    .load(std::memory_order_relaxed)
                    .value() >> region_chunk_handle_header_data_bits;
            }

            RegionAllocator* allocator{};
        };

        RegionChunkAllocation handle_to_allocation(uint32 region, uint32 handle) {
            CUW3_ASSERT(region < last_region(), "invalid region number");
            CUW3_ASSERT(handle < specs.num_handles, "invalid handle number");

            auto& region_specs = specs.region_specs[region];
            void* chunk_memory = advance_arr_log2(advance_ptr(regions, region_specs.region_offset), region_specs.chunk_size_log2, handle);
            void* handle_memory = advance_arr_log2(handles, specs.handle_size_log2, handle);
            return {chunk_memory, handle_memory};
        }

        RegionChunkLocation allocation_to_location(RegionChunkAllocation allocation) {
            if (allocation.handle < handles || allocation.handle >= advance_ptr(handles, specs.total_handles_size)) {
                return {};
            }            
            RegionChunkLocation location = locate_chunk(allocation.chunk);
            CUW3_ASSERT(valid_chunk_location(location), "invalid chunk location was computed on attempt to deallocate chunk");
            if (location.handle != index_from_handle(allocation.handle)) {
                return {};
            }
            return location;
        }

        uint32 search_pool_split(uint32 region, uint32 handle) {
            CUW3_ASSERT(region < specs.num_regions, "invalid region index");
            CUW3_ASSERT(handle < specs.num_handles, "invalid handle value");

            for (uint split = 0; split < contention_split; split++) {
                if (handle < pools.split_search_sentinels[region][split]) {
                    return split;
                }
            }
            return last_split();
        }

        [[nodiscard]] RegionChunkAllocation allocate_chunk(int attempts, uint32 region) {
            CUW3_ASSERT(region < last_region(), "invalid region value");

            for (uint split = 0; split < contention_split; split++) {
                auto& pool_entry = pools.pool_entries[region][split];

                auto list_view = RegionChunkPoolListView{&pool_entry.free_list};
                uint32 node = list_view.pop(attempts, Backoff{}, ExternalDataOps{this});
                if (node < list_view.op_failed) {
                    return handle_to_allocation(region, node);
                }
                if (node == list_view.op_failed) {
                    continue;
                }
                if (node == list_view.null_link) {
                    auto stack_view = RegionChunkPoolStackView{&pool_entry.free_stack, pool_entry.stack_limit};
                    node = stack_view.bump();
                }
                if (node < list_view.op_failed) {
                    return handle_to_allocation(region, node);
                }
            }
            return {};
        }
        
        void deallocate_chunk(RegionChunkLocation location) {
            uint32 split = 0;
            if (contention_split > 1) {
                split = search_pool_split(location.region, location.handle);
                CUW3_CHECK(split != last_split(), "failed to find split index of the deallocated region chunk.");
            }
            auto& pool_entry = pools.pool_entries[location.region][split];
            auto list_view = RegionChunkPoolListView{&pool_entry.free_list};
            list_view.push(location.handle, Backoff{}, ExternalDataOps{this});
        }
        
        
        // API
        bool safe_and_sound() const {
            return regions && handles;
        }

        bool belongs_any_region(void* ptr) const {
            return regions && regions <= ptr && ptr < advance_ptr(regions, specs.total_regions_size);
        }

        bool valid_chunk_location(RegionChunkLocation location) {
            if (location.region >= last_region() || location.handle >= specs.num_handles) {
                return false;
            }
            auto& region_specs = specs.region_specs[location.region];
            return location.chunk < region_specs.num_handles
                && region_specs.handle_offset <= location.handle && location.handle < region_specs.handle_offset + region_specs.num_handles;
        }

        uint32 last_split() const {
            return conf_max_contention_split;
        }

        uint32 last_region() const {
            return conf_max_region_sizes;
        }
        
        uint32 last_handle() const {
            return specs.num_handles;
        }

        [[nodiscard]] uint32 index_from_handle(void* handle) {
            if (handle < handles || handle >= advance_ptr(handles, specs.total_handles_size)) {
                return last_handle();
            }
            if (!is_aligned(handle, specs.handle_size)) {
                return last_handle();
            }
            return divpow2(subptr(handle, handles), specs.handle_size_log2);
        }

        [[nodiscard]] void* handle_from_index(uint32 index) {
            if (index >= specs.num_handles) {
                return nullptr;
            }
            return advance_arr_log2(handles, specs.handle_size_log2, index);
        }        

        [[nodiscard]] uint32 search_suitable_region(uint64 size) {
            for (usize i = 0; i < specs.num_regions; i++) {
                if (size <= specs.chunk_size_search_sentinels[i]) {
                    return i;
                }
            }
            return last_region();
        }

        [[nodiscard]] RegionChunkLocation locate_chunk(void* ptr) {
            if (!belongs_any_region(ptr)) {
                return {};
            }
            if (specs.region_size) {
                return locate_chunk_all_regions_equal(ptr);
            }
            return locate_chunk_all_regions_differ(ptr);
        }

        [[nodiscard]] RegionChunkAllocation allocate_chunk(uint32 region) {
            if (!safe_and_sound()) {
                return {};
            }
            if (region >= last_region()) {
                return {};
            }

            if (contention_split > 1) {
                if (auto allocation = allocate_chunk(chunk_pool_alloc_attempts, region)) {
                    return allocation;
                }
            }
            for (int i = 0; i < chunk_alloc_attempts; i++) {
                if (auto allocation = allocate_chunk(-1, region)) {
                    return allocation;
                }
            }
            return {};
        }

        void deallocate_chunk(RegionChunkAllocation allocation) {
            if (!safe_and_sound()) {
                return;
            }
            if (!allocation) {
                return;
            }

            auto location = allocation_to_location(allocation);
            if (location) {
                deallocate_chunk(location);
            }
        }

        // NOTE : if I even would like to implement deallocate_chunk_chain function I can remember that
        // next pointer is a function from handle memory location:
        // next = some_fancy_functor(handle)
        // void deallocate_chunk_chain(...) {}


        RegionAllocatorSpecs specs{};

        uint64 page_size{}; // readonly
        uint64 huge_page_size{}; // readonly
        uint64 contention_split{}; // readonly

        void* regions{};
        void* handles{};

        RegionAllocatorPools pools{};
    };
}