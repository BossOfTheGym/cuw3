#pragma once

#include "ptr.hpp"
#include "conf.hpp"
#include "funcs.hpp"
#include "assert.hpp"
#include "atomic.hpp"
#include "backoff.hpp"
#include "region_chunk_handle.hpp"

namespace cuw3 {
    inline constexpr uint32 region_chunk_allocator_null_value = 0xFFFFFFFF;
    inline constexpr uint32 region_chunk_allocator_failed_value = 0xFFFFFFFE;

    struct RegionChunkAllocatorSpecsConfig {
        const uint64* region_sizes{};
        uint64 num_region_sizes{};
        
        const uint64* region_chunk_sizes{};
        uint64 num_region_chunk_sizes{};
        
        uint64 handle_size{};
        uint64 region_alignment{};
        uint64 handle_alignment{};
    };

    struct RegionSpec {
        void* get_region(void* regions_base) const {
            return advance_ptr(regions_base, region_offset);
        }

        void* get_chunk(void* regions_base, uint32 chunk) const {
            CUW3_CHECK(check_chunk(chunk), "invalid chunk");

            return advance_ptr(regions_base, get_relchunk(chunk));
        }

        void* get_handle(void* handles_base, uint32 handle) const {
            CUW3_CHECK(check_handle(handle), "invalid handle");

            return advance_arr_log2(handles_base, handle_size_log2, handle);
        }

        bool check_chunk(uint32 chunk) const {
            return chunk < num_handles;
        }

        bool check_handle(uint32 handle) const {
            return handle_offset <= handle && handle < handle_offset + num_handles;
        }

        bool is_empty() const {
            return num_handles == 0;
        }

        uint64 get_chunk_size() const {
            return intpow2(chunk_size_log2);
        }

        uint32 get_first_handle() const {
            return handle_offset;
        }

        uint32 get_last_handle() const {
            return handle_offset + num_handles;
        }

        uint64 get_first_offset() const {
            return region_offset;
        }

        uint64 get_last_offset() const {
            return region_offset + region_size;
        }

        uint64 get_relchunk(uint32 chunk) const {
            return region_offset + mulpow2(chunk, chunk_size_log2);
        }

        uint64 region_offset{}; // byte offset of the region start
        uint64 region_size{}; // byte size of the region
        uint64 chunk_size_log2{}; // intlog2(region_chunk_size)
        uint64 handle_size_log2{};// NOTE: all regions have handles of equal sizes (ALL OF THEM!)
        uint32 handle_offset{}; // handle offset (index) of the first handle
        uint32 num_handles{}; // number of handles (or chunks) within a region
    };
    
    struct RegionChunkLocation {
        explicit operator bool() const {
            return region != region_chunk_allocator_null_value;
        }

        uint32 region{}; // region number
        uint32 chunk{}; // chunk index within the region, also relative index of the handle
        uint32 handle{}; // global index of the handle
    };

    // NOTE : this is, I think, the most simple version of what can be done
    // Of course, this implementation can be made more adaptable to real-world circumstances
    // Instead of preallocated range of reserved virtual memory we can have several slots (pointers)  
    // Like, one slot points to some allocation of predefined amount of space. We add another one I we truely need it.
    // This may help to save some virtual memory space as well ... it may become an issue with this allocator
    //
    // stores layout of the regions and handles
    // this is a read-only data structure
    struct RegionChunkAllocatorSpecs {
        // not an error if some region has zero chunks (region size is less than size of a single chunk)
        [[nodiscard]] static RegionChunkAllocatorSpecs* create(Memory memory, const RegionChunkAllocatorSpecsConfig& config) {
            CUW3_CHECK_RETURN_VAL(memory.fits<RegionChunkAllocatorSpecs>(), nullptr, "memory was null");

            CUW3_CHECK_RETURN_VAL(0 < config.num_region_sizes && config.num_region_sizes <= conf_max_region_sizes, nullptr, "invalid number of regions");
            CUW3_CHECK_RETURN_VAL(config.num_region_sizes == config.num_region_chunk_sizes, nullptr, "num of regions and num of region chunk sizes mismatch");
            CUW3_CHECK_RETURN_VAL(is_pow2(config.handle_size), nullptr, "handle_size mut be power of 2");
            CUW3_CHECK_RETURN_VAL(is_alignment(config.region_alignment), nullptr, "invalid alignment value for region_storage");
            CUW3_CHECK_RETURN_VAL(is_alignment(config.handle_alignment), nullptr, "invalid handle storage alignment");
            CUW3_CHECK_RETURN_VAL(is_aligned(config.handle_size, config.handle_alignment), nullptr, "handle_size is not aligned");

            bool all_region_sizes_equal = all_equal(config.region_sizes, config.region_sizes + config.num_region_sizes);
            uint64 region_size = all_region_sizes_equal ? align(intpow2(config.region_sizes[0]), config.region_alignment) : 0;
            uint64 region_size_log2 = all_region_sizes_equal ? pow2log2(region_size) : 0;

            uint64 handle_size_log2 = pow2log2(config.handle_size);

            auto* specs = new (memory.get()) RegionChunkAllocatorSpecs{};
            specs->region_alignment = config.region_alignment;
            specs->region_size = region_size;
            specs->region_size_log2 = region_size_log2;
            specs->num_regions = config.num_region_sizes;

            specs->handle_alignment = config.handle_alignment;
            specs->handle_size = config.handle_size;
            specs->handle_size_log2 = handle_size_log2;

            uint32 handle_offset = 0;
            uint64 region_offset = 0;
            for (usize i = 0; i < config.num_region_sizes; i++) {
                uint64 region_size = align(intpow2(config.region_sizes[i]), config.region_alignment);
                uint64 chunk_size_log2 = config.region_chunk_sizes[i];
                uint32 num_handles = divpow2(region_size, chunk_size_log2);

                specs->region_specs[i] = {
                    .region_offset = region_offset,
                    .region_size = region_size,
                    .chunk_size_log2 = chunk_size_log2,
                    .handle_size_log2 = handle_size_log2,
                    .handle_offset = handle_offset,
                    .num_handles = num_handles,
                };

                specs->chunk_size_search_sentinels[i] = intpow2(chunk_size_log2); 
                specs->region_search_sentinels[i] = region_offset + region_size;

                handle_offset += num_handles;
                region_offset += region_size;
            }

            specs->total_regions_size = region_offset;
            specs->total_handles_size = align(mulpow2(handle_offset, specs->handle_size_log2), specs->handle_alignment);
            specs->num_handles = handle_offset;

            specs->total_pool_handles_size = sizeof(RegionChunkPoolLinkType) * specs->num_handles;
            specs->pool_handles_alignment = alignof(RegionChunkPoolLinkType);

            return specs;
        }


        [[nodiscard]] RegionChunkLocation _locate_chunk_common(uint64 relptr, uint32 region) const {
            auto& specs = region_specs[region];
            CUW3_CHECK(specs.num_handles != 0, "we attempted to locate chunk within an empty region.");

            uint64 rel_to_region = relptr - specs.region_offset;
            uint32 chunk = divpow2(rel_to_region, specs.chunk_size_log2);
            uint32 handle = specs.handle_offset + chunk;
            return {region, chunk, handle};
        }

        [[nodiscard]] RegionChunkLocation _locate_chunk_all_regions_equal(uint64 relptr) const {
            CUW3_CHECK(region_size != 0, "regions are not equal");

            uint32 region = divpow2(relptr, region_size_log2);
            return _locate_chunk_common(relptr, region);
        }

        [[nodiscard]] RegionChunkLocation _locate_chunk_all_regions_differ(uint64 relptr) const {
            CUW3_ASSERT(region_size == 0, "all regions are equal: more effective algorithm available.");

            uint32 region = search_hosting_region(relptr);
            CUW3_CHECK(region != region_chunk_allocator_null_value, "unreachable reached: sentinel search exausted");

            return _locate_chunk_common(relptr, region);
        }


        [[nodiscard]] uint32 search_hosting_region(uint64 relptr) const {
            for (uint i = 0; i < num_regions; i++) {
                if (relptr < region_search_sentinels[i]) {
                    return i;
                }
            }
            return region_chunk_allocator_null_value;
        }

        [[nodiscard]] uint32 search_suitable_region(uint64 size) const {
            for (usize i = 0; i < num_regions; i++) {
                if (size <= chunk_size_search_sentinels[i]) {
                    return i;
                }
            }
            return region_chunk_allocator_null_value;
        }

        [[nodiscard]] RegionChunkLocation locate_chunk(uint64 relptr) const {
            if (region_size) {
                return _locate_chunk_all_regions_equal(relptr);
            }
            return _locate_chunk_all_regions_differ(relptr);
        }

        bool is_valid_handle(uint32 handle) const {
            return handle < num_handles;
        }

        void* get_handle(void* handles_base, uint32 handle) const {
            CUW3_CHECK(is_valid_handle(handle), "invalid handle");

            return advance_arr_log2(handles_base, handle_size_log2, handle);
        }


        RegionSpec region_specs[conf_max_region_sizes] = {}; // readonly
        
        // NOTE: AVX can be used here to facilitate searching
        uint64 chunk_size_search_sentinels[conf_max_region_sizes] = {}; // readonly

        // NOTE: AVX can be used here to facilitate searching
        // NOTE: we would like to make this as fast as possible because we call this function on each deallocation
        uint64 region_search_sentinels[conf_max_region_sizes] = {}; // readonly

        uint64 total_regions_size{}; // readonly, in bytes
        uint64 region_alignment{}; // readonly
        uint64 region_size{}; // readonly, in bytes, in case all regions are equal otherwise zero
        uint64 region_size_log2{}; // readonly, intlog2(region_size) in case all regions are equal otherwise zero
        uint64 num_regions{}; // readonly
        
        // pool metadata is stored separately from the handle
        uint64 total_handles_size{}; // readonly, in bytes
        uint64 handle_alignment{}; // readonly
        uint64 handle_size{}; // readonly, in bytes
        uint64 handle_size_log2{}; // readonly
        uint64 num_handles{}; // readonly

        uint64 total_pool_handles_size{}; // readonly, in bytes
        uint64 pool_handles_alignment{}; // readonly, in bytes
    };



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

    using RegionChunkAllocatorBackoff = SimpleBackoff;

    struct alignas(conf_cacheline) RegionPoolEntry {
        RegionChunkPoolListHead free_list{}; // atomic
        RegionChunkPoolStackTop free_stack{}; // atomic
        RegionChunkPoolLinkType first_handle{}; // readonly
        RegionChunkPoolLinkType last_handle{}; // readonly
    };

    struct RegionChunkAllocatorPoolsConfig {
        const RegionChunkAllocatorSpecs* specs{};
        uint64 contention_split{};
    };

    struct RegionChunkAllocatorPools {
        [[nodiscard]] static RegionChunkAllocatorPools* create(Memory memory, const RegionChunkAllocatorPoolsConfig& config) {
            CUW3_CHECK_RETURN_VAL(memory.fits<RegionChunkAllocatorPools>(), nullptr, "invalid memory");
            CUW3_CHECK_RETURN_VAL(config.contention_split <= conf_max_contention_split, nullptr, "invalid value of contention split");
            CUW3_CHECK_RETURN_VAL(is_pow2(config.contention_split), nullptr, "contention split must be power of two");

            const RegionChunkAllocatorSpecs* specs = config.specs;
            uint64 contention_split = config.contention_split ? config.contention_split : 1;

            auto* pools = new (memory.get()) RegionChunkAllocatorPools{};
            pools->num_regions = config.specs->num_regions;
            pools->num_splits = contention_split;

            // this will initialize all not used entries as well
            // all unused entries will be empty: valid state (empty list, empty stack) but nothing to allocate
            for (usize pool = 0; pool < conf_max_region_sizes; pool++) {
                auto& region_specs = specs->region_specs[pool];

                uint32 handles_per_split = (region_specs.num_handles + contention_split - 1) / contention_split;
                for (usize split = 0; split < conf_max_contention_split; split++) {
                    uint64 first_handle = region_specs.handle_offset + std::min<uint32>(handles_per_split * split, region_specs.num_handles);
                    uint64 last_handle = region_specs.handle_offset + std::min<uint32>(handles_per_split * (split + 1), region_specs.num_handles);

                    auto& pool_entry = pools->pool_entries[pool][split];
                    pool_entry.free_list = {0, region_chunk_pool_null_link};
                    pool_entry.free_stack = first_handle;
                    pool_entry.first_handle = first_handle;
                    pool_entry.last_handle = last_handle;

                    pools->split_search_sentinels[pool][split] = last_handle;
                }
            }
            return pools;
        }


        // step = 0 used to initialize start value only so beware
        uint32 next_split(uint32 split, uint32 step = 0) {
            CUW3_CHECK(step == 0 || step % 2 == 1, "invalid step value");

            return (split + step) & (num_splits - 1);
        }

        uint32 search_pool_split(uint32 region, uint32 handle) {
            CUW3_CHECK(region < conf_max_region_sizes, "invalid region index");

            for (uint split = 0; split < num_splits; split++) {
                if (handle < split_search_sentinels[region][split]) {
                    return split;
                }
            }
            return region_chunk_allocator_null_value;
        }


        template<class PoolOps>
        [[nodiscard]] RegionChunkPoolLinkType allocate_from_list(uint32 region, uint32 split, int alloc_attempts, PoolOps&& ops) {
            CUW3_CHECK(region < num_regions, "invalid region");
            CUW3_CHECK(split < num_splits, "invalid split");

            auto& entry = pool_entries[region][split];
            auto list_view = RegionChunkPoolListView{&entry.free_list};
            return list_view.pop(alloc_attempts, RegionChunkAllocatorBackoff{}, ops);
        }

        RegionChunkPoolLinkType allocate_from_stack(uint32 region, uint32 split) {
            CUW3_CHECK(region < num_regions, "invalid region");
            CUW3_CHECK(split < num_splits, "invalid split");

            auto& pool_entry = pool_entries[region][split];
            auto stack_view = RegionChunkPoolStackView{&pool_entry.free_stack, pool_entry.last_handle};
            return stack_view.bump();
        }

        template<class PoolOps>
        void deallocate(RegionChunkPoolLinkType handle, uint32 region, uint32 split, PoolOps&& ops) {
            CUW3_CHECK(region < num_regions, "invalid region");
            CUW3_CHECK(split < num_splits, "invalid split");

            auto& pool_entry = pool_entries[region][split];
            auto list_view = RegionChunkPoolListView{&pool_entry.free_list};
            list_view.push(handle, RegionChunkAllocatorBackoff{}, ops);
        }


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
        uint64 num_regions{}; // readonly
        uint64 num_splits{}; // readonly, pow of 2
    };


    inline constexpr int region_allocator_alloc_rounds = 4;
    inline constexpr int region_allocator_alloc_attempts = 2;

    struct RegionChunkAllocParams {
        int rounds = -1; // chunk allocation attempts
        int attempts = -1; // pool allocation attempts
        uint split_start = 0;
        uint split_step = 1;
    };

    // TODO : must include chunk size
    struct RegionChunkAllocation {
        explicit operator bool() const {
            return valid();
        }

        bool null() const {
            return region == region_chunk_allocator_null_value;
        }

        bool failed() const {
            return region == region_chunk_allocator_failed_value;
        }

        bool valid() const {
            return !null() && !failed();
        }

        uint32 region{};
        uint32 chunk{}; // dementia comment: seems like it is chunk index within the region
        uint32 handle{}; // absolute handle index
        uint32 split{};
    };

    // TODO : reconsider naming
    struct RegionChunkMemory {
        explicit operator bool() const {
            return valid();
        }

        bool valid() const {
            return chunk;
        }

        void* chunk{};
        void* handle{};
    };

    struct RegionAllocatorConfig {
        const RegionChunkAllocatorSpecs* specs{};
        RegionChunkAllocatorPools* pools{};
        void* regions{};
        void* handles{};
        void* pool_handles{};
    };

    // NOTE : we can add here cache that will store some retired region chunks
    //   so we can fastly store them there without decommitting memory
    //   and we can fastly acquire them from there without committing memory
    // NOTE : we can commit them not all at once but sequentially when needed
    //
    // this is commonly a global shared entity
    struct RegionChunkAllocator {
        struct RegionAllocatorPoolHandleOps {
            RegionChunkPoolLinkType* get_handle(RegionChunkPoolLinkType node) {
                return (RegionChunkPoolLinkType*)alloc->pool_handles + node;
            }

            // exclusive modification here while we attempt to push it back into the list
            void set_next(RegionChunkPoolLinkType node, RegionChunkPoolLinkType next) {
                CUW3_CHECK(node < alloc->specs->num_handles, "invalid link value 'node' passed");
                CUW3_CHECK(next < alloc->specs->num_handles || next == region_chunk_pool_null_link, "invalid link value 'next' passed");

                std::atomic_ref{*get_handle(node)}.store(next, std::memory_order_relaxed);
            }

            RegionChunkPoolLinkType get_next(RegionChunkPoolLinkType node) {
                CUW3_CHECK(node < alloc->specs->num_handles, "invalid node handle passed");

                return std::atomic_ref{*get_handle(node)}.load(std::memory_order_relaxed);
            }

            RegionChunkAllocator* alloc{};
        };


        [[nodiscard]] static RegionChunkAllocator* create(Memory memory, const RegionAllocatorConfig& config) {
            CUW3_CHECK_RETURN_VAL(memory.fits<RegionChunkAllocator>(), nullptr, "invalid memory");

            auto* alloc = new (memory.get()) RegionChunkAllocator{};
            alloc->specs = config.specs;
            alloc->pools = config.pools;
            alloc->regions = config.regions;
            alloc->handles = config.handles;
            alloc->pool_handles = config.pool_handles;
            return alloc;
        }


        uint32 _region_handle_to_chunk(uint32 region, uint32 handle) {
            CUW3_ASSERT(region < specs->num_regions, "invalid region value");
            CUW3_ASSERT(handle < specs->num_handles, "invalid handle value");

            auto& region_specs = specs->region_specs[region];
            CUW3_ASSERT(!region_specs.is_empty(), "empty region");
            CUW3_ASSERT(region_specs.check_handle(handle), "handle does not belong to the region");

            return handle - region_specs.handle_offset;
        }

        RegionChunkMemory _region_handle_to_memory(uint32 region, uint32 handle) {
            uint32 chunk = _region_handle_to_chunk(region, handle);

            auto& region_specs = specs->region_specs[region];
            void* chunk_memory = region_specs.get_chunk(regions, chunk);
            void* handle_memory = region_specs.get_handle(handles, handle);
            return {chunk_memory, handle_memory};
        }

        [[nodiscard]] RegionChunkAllocation _allocate_chunk(uint32 region, RegionChunkAllocParams alloc_params) {
            CUW3_ASSERT(region < specs->num_regions, "invalid region value");

            bool chunk_seen = false;
            for (
                uint k = 0, split = pools->next_split(alloc_params.split_start);
                k < pools->num_splits;
                k++, split = pools->next_split(split, alloc_params.split_step)
            ) {
                uint32 handle = pools->allocate_from_list(region, split, alloc_params.attempts, RegionAllocatorPoolHandleOps{this});
                if (handle < specs->num_handles) {
                    return {region, _region_handle_to_chunk(region, handle), handle, split};
                }
                if (handle == region_chunk_allocator_failed_value) {
                    chunk_seen = true;
                    continue;
                }
                if (handle == region_chunk_allocator_null_value) {
                    handle = pools->allocate_from_stack(region, split);
                }
                if (handle < specs->num_handles) {
                    return {region, _region_handle_to_chunk(region, handle), handle, split};
                }
                // no need to check op_failed for current stack implementation
            }
            return chunk_seen
                ? RegionChunkAllocation{region_chunk_allocator_failed_value}
                : RegionChunkAllocation{region_chunk_allocator_null_value};
        }
        
        
        // API
        bool is_valid_handle(void* handle) const {
            auto handle_int = (uintptr)handle;
            auto handles_int = (uintptr)handles;
            auto handles_end_int = (uintptr)advance_ptr(handles, specs->total_handles_size);
            return handles_int <= handle_int && handle_int < handles_end_int;
        }

        bool belongs_any_region(void* ptr) const {
            auto ptr_int = (uintptr)ptr;
            auto regions_int = (uintptr)regions;
            auto regions_end_int = (uintptr)advance_ptr(regions, specs->total_regions_size);
            return regions_int <= ptr_int && ptr_int < regions_end_int;
        }

        uint64 get_max_chunk_size() const {
            return specs->region_specs[specs->num_regions - 1].get_chunk_size();
        }

        uint64 get_num_regions() const {
            return specs->num_regions;
        }

        const RegionSpec& get_region_spec(uint64 region) const {
            CUW3_CHECK(region < specs->num_regions, "invalid region");
            return specs->region_specs[region];
        }

        [[nodiscard]] uint32 index_from_handle(void* handle) {
            if (!is_valid_handle(handle)) {
                return region_chunk_allocator_null_value;
            }
            if (!is_aligned(handle, specs->handle_size)) {
                return region_chunk_allocator_null_value;
            }
            return divpow2(subptr(handle, handles), specs->handle_size_log2);
        }

        [[nodiscard]] void* handle_from_index(uint32 handle_id) {
            if (!specs->is_valid_handle(handle_id)) {
                return nullptr;
            }
            return specs->get_handle(handles, handle_id);
        }

        // TODO : rename & reconsider input parameters
        [[nodiscard]] RegionChunkMemory region_data_to_memory_no_check(uint32 region, uint32 chunk, uint32 handle) {
            auto& region_specs = specs->region_specs[region];
            void* chunk_mem = region_specs.get_chunk(regions, chunk);
            void* handle_mem = specs->get_handle(handles, handle);
            return {chunk_mem, handle_mem};
        }

        // TODO : rename & reconsider input parameters
        [[nodiscard]] RegionChunkMemory region_data_to_memory(uint32 region, uint32 chunk, uint32 handle) {
            CUW3_CHECK_RETURN_VAL(region < specs->num_regions, {}, "invalid region");
            CUW3_CHECK_RETURN_VAL(handle < specs->num_handles, {}, "invalid handle");

            auto& region_specs = specs->region_specs[region];
            CUW3_CHECK_RETURN_VAL(!region_specs.is_empty(), {}, "region is empty");
            CUW3_CHECK_RETURN_VAL(region_specs.check_chunk(chunk), {}, "invalid chunk");
            CUW3_CHECK_RETURN_VAL(region_specs.check_handle(handle), {}, "invalid handle(2)");
            
            return region_data_to_memory_no_check(region, chunk, handle);
        }

        [[nodiscard]] uint32 search_suitable_region(uint64 size) {
            return specs->search_suitable_region(size);
        }

        [[nodiscard]] RegionChunkLocation ptr_to_location(void* ptr) {
            if (!belongs_any_region(ptr)) {
                return {};
            }
            return specs->locate_chunk(subptr(ptr, regions));
        }

        [[nodiscard]] RegionChunkAllocation ptr_to_allocation(void* ptr) {
            RegionChunkLocation location = ptr_to_location(ptr);
            if (!location) {
                return {region_chunk_allocator_null_value};
            }

            uint32 split = pools->search_pool_split(location.region, location.handle);
            CUW3_CHECK(split != region_chunk_allocator_null_value, "invalid split value");

            return {location.region, location.chunk, location.handle, split};
        }

        [[nodiscard]] RegionChunkAllocation allocate_chunk(uint32 region, RegionChunkAllocParams alloc_params) {
            if (region >= specs->num_regions) {
                return {region_chunk_allocator_null_value};
            }

            RegionChunkAllocatorBackoff backoff{};
            for (int rounds = alloc_params.rounds; rounds != 0; ) {
                auto allocation = _allocate_chunk(region, alloc_params);
                if (allocation) {
                    return allocation;
                }
                if (allocation.null()) {
                    rounds -= rounds > 0;
                }
                backoff();
            }
            return {region_chunk_allocator_null_value};
        }

        void deallocate_chunk(RegionChunkAllocation location) {
            CUW3_CHECK(location.region < specs->num_regions, "invalid region value");
            CUW3_CHECK(location.split < pools->num_splits, "invalid split value");

            auto& region_specs = specs->region_specs[location.region];
            CUW3_CHECK(region_specs.check_chunk(location.chunk), "invalid chunk value");
            CUW3_CHECK(region_specs.check_handle(location.handle), "invalid handle value");

            pools->deallocate(location.handle, location.region, location.split, RegionAllocatorPoolHandleOps{this});
        }

        void deallocate_chunk(RegionChunkMemory memory) {
            if (!memory) {
                return;
            }
            if (auto allocation = ptr_to_allocation(memory.chunk)) {
                deallocate_chunk(allocation);
            }
        }

        // NOTE : if I wanted to implement deallocate_chunk_chain function I can remember that
        // next pointer is a function from handle memory location:
        // next = some_fancy_functor(handle)
        // void deallocate_chunk_chain(...) {}

        const RegionChunkAllocatorSpecs* specs{};
        RegionChunkAllocatorPools* pools{};
        void* regions{};
        void* handles{};
        void* pool_handles{};
    };
}