#pragma once

#include "ptr.hpp"
#include "conf.hpp"
#include "funcs.hpp"
#include "assert.hpp"
#include "atomic.hpp"
#include "backoff.hpp"
#include "region_chunk_handle.hpp"

// TODO : maybe unite specs & pools & state
namespace cuw3 {
    inline constexpr uint32 region_chunk_allocator_null_value = 0xFFFFFFFF;
    inline constexpr uint32 region_chunk_allocator_failed_value = 0xFFFFFFFF;

    struct RegionChunkAllocatorSpecsConfig {
        void* memory{}; // TODO : memory size

        const uint64* region_sizes{};
        uint64 num_region_sizes{};
        
        const uint64* region_chunk_sizes{};
        uint64 num_region_chunk_sizes{};
        
        uint64 handle_size{};
        uint64 region_storage_alignment{};
        uint64 handle_storage_alignment{};
    };

    struct RegionSpec {
        uint64 region_offset{}; // byte offset of the region start
        uint64 region_size{}; // byte size of the region
        uint64 chunk_size_log2{}; // intlog2(region_chunk_size)
        uint32 handle_offset{}; // handle offset (index) of the first handle
        uint32 num_handles{}; // number of handles (or chunks) within a region
    };
    
    struct RegionChunkLocation {
        explicit operator bool() const {
            return region == region_chunk_allocator_null_value;
        }

        uint32 region{}; // region number
        uint32 chunk{}; // chunk index within the region, also relative index of the handle
        uint32 handle{}; // global index of the handle
    };

    // NOTE : this is, I think, the most simple version of what can be done
    // Of course, this implementation can be made more adaptable to real-world circumstances
    // Instead of preallocated range of reserved virtual memory we can have several slots (pointers)  
    // Like, one slot points to some allocation of predifined amount of space. We add another one I we truely nned it.
    // This may help to save some virtual emory space as ... it may become an issue with this allocator 
    //
    // stores layout of the regions and handles
    // this is a read-only data structure
    struct RegionChunkAllocatorSpecs {
        // not an error if some region has zero chunks (region size is less than size of a single chunk)
        [[nodiscard]] static RegionChunkAllocatorSpecs* create(const RegionChunkAllocatorSpecsConfig& config) {
            CUW3_CHECK_RETURN_VAL(config.memory, nullptr, "memory was null");
            CUW3_CHECK_RETURN_VAL(0 < config.num_region_sizes && config.num_region_sizes <= conf_max_region_sizes, nullptr, "invalid number of regions");
            CUW3_CHECK_RETURN_VAL(config.num_region_sizes == config.num_region_chunk_sizes, nullptr, "num of regions and num of region chunk sizes mismatch");
            CUW3_CHECK_RETURN_VAL(is_pow2(config.handle_size), nullptr, "handle_size mut be power of 2");
            CUW3_CHECK_RETURN_VAL(is_alignment(config.region_storage_alignment), nullptr, "invalid alignment value for region_storage");
            CUW3_CHECK_RETURN_VAL(is_alignment(config.handle_storage_alignment), nullptr, "invalid handle storage alignment");

            bool all_region_sizes_equal = all_equal(config.region_sizes, config.region_sizes + config.num_region_sizes);
            uint64 region_size = all_region_sizes_equal ? align(intpow2(config.region_sizes[0]), config.region_storage_alignment) : 0;
            uint64 region_size_log2 = all_region_sizes_equal ? pow2log2(region_size) : 0;

            auto* specs = new (config.memory) RegionChunkAllocatorSpecs{};
            specs->region_alignment = config.region_storage_alignment;
            specs->region_size = region_size;
            specs->region_size_log2 = region_size_log2;
            specs->num_regions = config.num_region_sizes;

            specs->handle_alignment = config.handle_storage_alignment;
            specs->handle_size = config.handle_size;
            specs->handle_size_log2 = pow2log2(config.handle_size);

            uint32 handle_offset = 0;
            uint64 region_offset = 0;
            for (usize i = 0; i < config.num_region_sizes; i++) {
                uint64 region_size = align(intpow2(config.region_sizes[i]), config.region_storage_alignment);
                uint64 chunk_size_log2 = config.region_chunk_sizes[i];
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
            specs->total_handles_size = align(mulpow2(handle_offset, specs->handle_size_log2), specs->handle_alignment);
            specs->num_handles = handle_offset;
            return specs;
        }


        [[nodiscard]] uint32 search_hosting_region(uint64 relptr) const {
            for (uint i = 0; i < num_regions; i++) {
                if (relptr < region_search_sentinels[i]) {
                    return i;
                }
            }
            return region_chunk_allocator_null_value;
        }

        [[nodiscard]] uint32 search_suitable_region(uint64 size) {
            for (usize i = 0; i < num_regions; i++) {
                if (size <= chunk_size_search_sentinels[i]) {
                    return i;
                }
            }
            return region_chunk_allocator_null_value;
        }

        [[nodiscard]] RegionChunkLocation locate_chunk_common(uint64 relptr, uint32 region) const {
            auto& specs = region_specs[region];
            CUW3_CHECK(specs.num_handles != 0, "we attempted to locate chunk within an empty region.");

            uint64 rel_to_region = relptr - specs.region_offset;
            uint32 chunk = divpow2(rel_to_region, specs.chunk_size_log2);
            uint32 handle = specs.handle_offset + chunk;
            return {region, chunk, handle};
        }

        [[nodiscard]] RegionChunkLocation locate_chunk_all_regions_equal(uint64 relptr) const {
            CUW3_CHECK(region_size != 0, "regions are not equal");

            uint32 region = divpow2(relptr, region_size_log2);
            return locate_chunk_common(relptr, region);
        }

        [[nodiscard]] RegionChunkLocation locate_chunk_all_regions_differ(uint64 relptr) const {
            CUW3_ASSERT(region_size == 0, "all regions are equal: more effective algorithm available.");

            uint32 region = search_hosting_region(relptr);
            CUW3_CHECK(region != region_chunk_allocator_null_value, "unreachable reached: sentinel search exausted");

            return locate_chunk_common(relptr, region);
        }

        [[nodiscard]] RegionChunkLocation locate_chunk(uint64 relptr) const {
            if (region_size) {
                return locate_chunk_all_regions_equal(relptr);
            }
            return locate_chunk_all_regions_differ(relptr);
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
        
        uint64 total_handles_size{}; // readonly, in bytes
        uint64 handle_alignment{}; // readonly
        uint64 handle_size{}; // readonly, in bytes
        uint64 handle_size_log2{}; // readonly
        uint64 num_handles{}; // readonly
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

    struct alignas(conf_cacheline) RegionPoolEntry {
        RegionChunkPoolListHead free_list{}; // atomic memory location
        RegionChunkPoolStackTop free_stack{}; // atomic memory location
        RegionChunkPoolLinkType stack_limit{}; // readonly memory location
    };

    struct RegionChunkAllocatorPoolsConfig {
        void* memory{};
        // TODO : memory size

        const RegionChunkAllocatorSpecs* allocator_specs{};
        uint64 contention_split{};
    };

    struct RegionChunkAllocatorPools {
        [[nodiscard]] static RegionChunkAllocatorPools* create(const RegionChunkAllocatorPoolsConfig& config) {
            CUW3_CHECK_RETURN_VAL(config.contention_split <= conf_max_contention_split, nullptr, "invalid value of contention split");
            CUW3_CHECK_RETURN_VAL(is_pow2(config.contention_split), nullptr, "");

            const RegionChunkAllocatorSpecs* specs = config.allocator_specs;
            uint64 contention_split = config.contention_split ? config.contention_split : 1;

            auto* pools = new (config.memory) RegionChunkAllocatorPools{};
            pools->contention_split = contention_split;

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
                    pool_entry.stack_limit = last_handle;

                    pools->split_search_sentinels[pool][split] = last_handle;
                }
            }
            return pools;
        }


        // step = 0 used to initialize start value only so beware
        uint32 next_split(uint32 split, uint32 step = 0) {
            CUW3_ASSERT(step == 0 || step % 2 == 1, "invalid step value");
            return (split + step) & contention_split - 1;
        }

        uint32 search_pool_split(uint32 region, uint32 handle) {
            CUW3_ASSERT(region < conf_max_region_sizes, "invalid region index");

            for (uint split = 0; split < contention_split; split++) {
                if (handle < split_search_sentinels[region][split]) {
                    return split;
                }
            }
            return region_chunk_allocator_null_value;
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

        uint64 contention_split{}; // readonly, power of 2
    };


    using RegionChunkAllocatorBackoff = SimpleBackoff;

    inline constexpr int region_allocator_alloc_rounds = 4;
    inline constexpr int region_allocator_alloc_attempts = 2;

    struct RegionChunkAllocParams {
        int rounds = -1;
        int attempts = -1;
        uint split_start = 0;
        uint split_step = 1;
    };

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
        uint32 chunk{};
        uint32 handle{};
        uint32 split{};
    };

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
        void* memory{}; // TODO : memory size

        const RegionChunkAllocatorSpecs* specs{};
        RegionChunkAllocatorPools* pools{};
        void* regions{}; // TODO : memory size
        void* handles{}; // TODO : memory size
    };

    // NOTE : we can add here cache that will store some retired region chunks
    //   so we can fastly store them there without decommitting memory
    //   and we can fastly acquire them from there without committing memory
    // NOTE : we can commit them not all at once but sequentially when needed
    // this is commonly a global shared entity
    struct RegionAllocator {
        struct NodeOps {
            RegionChunkHandleHeader* get_handle(RegionChunkPoolLinkType node) {
                return (RegionChunkHandleHeader*)advance_arr_log2(alloc->handles, alloc->specs->handle_size_log2, node);
            }

            // exclusive modification here while we attempt to push it back into the list
            void set_next(RegionChunkPoolLinkType node, RegionChunkPoolLinkType next) {
                CUW3_ASSERT(node < alloc->specs->num_handles, "invalid link value 'node' passed");
                CUW3_ASSERT(next < alloc->specs->num_handles || next == region_chunk_pool_null_link, "invalid link value 'next' passed");

                RegionChunkHandleHeaderView{get_handle(node)}.set_next_chunk(next);
            }

            RegionChunkPoolLinkType get_next(RegionChunkPoolLinkType node) {
                CUW3_ASSERT(node < alloc->specs->num_handles, "invalid node handle passed");

                return RegionChunkHandleHeaderView{get_handle(node)}.get_next_chunk();
            }

            RegionAllocator* alloc{};
        };


        [[nodiscard]] static RegionAllocator* create(const RegionAllocatorConfig& config) {
            auto* alloc = new (config.memory) RegionAllocator{};
            alloc->specs = config.specs;
            alloc->pools = config.pools;
            alloc->regions = config.regions;
            alloc->handles = config.handles;
            return alloc;
        }


        uint32 _region_handle_to_chunk(uint32 region, uint32 handle) {
            CUW3_ASSERT(region < specs->num_regions, "invalid region value");
            CUW3_ASSERT(handle < specs->num_handles, "invalid handle value");

            auto& region_specs = specs->region_specs[region];
            CUW3_ASSERT(region_specs.num_handles > 0, "empty region");
            CUW3_ASSERT(region_specs.handle_offset <= handle && handle < region_specs.handle_offset + region_specs.num_handles, "handle does not belong to the region");

            return handle - region_specs.handle_offset;
        }

        RegionChunkMemory _region_handle_to_memory(uint32 region, uint32 handle) {
            uint32 chunk = handle - _region_handle_to_chunk(region, handle);
            
            auto& region_specs = specs->region_specs[region];
            void* chunk_memory = advance_arr_log2(advance_ptr(regions, region_specs.region_offset), region_specs.chunk_size_log2, chunk);
            void* handle_memory = advance_arr_log2(handles, specs->handle_size_log2, handle);
            return {chunk_memory, handle_memory};
        }

        [[nodiscard]] RegionChunkAllocation _allocate_chunk(uint32 region, RegionChunkAllocParams alloc_params) {
            CUW3_ASSERT(region < specs->num_regions, "invalid region value");

            bool chunk_seen = false;
            for (uint k = 0, split = pools->next_split(alloc_params.split_start); k < pools->contention_split; split = pools->next_split(split, alloc_params.split_step)) {
                auto& pool_entry = pools->pool_entries[region][split];

                auto list_view = RegionChunkPoolListView{&pool_entry.free_list};
                uint32 handle = list_view.pop(alloc_params.attempts, RegionChunkAllocatorBackoff{}, NodeOps{this});
                if (handle < list_view.op_failed) {
                    return {region, _region_handle_to_chunk(region, handle), handle, split};
                }
                if (handle == list_view.op_failed) {
                    chunk_seen = true;
                    continue;
                }
                if (handle == list_view.null_link) {
                    auto stack_view = RegionChunkPoolStackView{&pool_entry.free_stack, pool_entry.stack_limit};
                    handle = stack_view.bump();
                }
                if (handle < list_view.op_failed) {
                    return {region, _region_handle_to_chunk(region, handle), handle, split};
                }
                if (handle == list_view.op_failed) { // even though currrent implementation does not return op_failed we check it
                    chunk_seen = true;
                    continue;
                }
            }
            return chunk_seen
                ? RegionChunkAllocation{region_chunk_allocator_failed_value}
                : RegionChunkAllocation{region_chunk_allocator_null_value};
        }
        
        void _deallocate_chunk(RegionChunkAllocation location) {
            CUW3_ASSERT(location.region < specs->num_regions, "invalid region value");
            CUW3_ASSERT(location.split < pools->contention_split, "invalid split value");

            auto& region_specs = specs->region_specs[location.region];
            CUW3_ASSERT(location.chunk < region_specs.num_handles, "invalid chunk value");
            CUW3_ASSERT(region_specs.handle_offset <= location.handle && location.handle < region_specs.handle_offset + region_specs.num_handles, "invalid handle value");

            auto& pool_entry = pools->pool_entries[location.region][location.split];
            auto list_view = RegionChunkPoolListView{&pool_entry.free_list};
            list_view.push(location.handle, RegionChunkAllocatorBackoff{}, NodeOps{this});
        }
        
        
        // API
        bool belongs_any_region(void* ptr) const {
            return regions <= ptr && ptr < advance_ptr(regions, specs->total_regions_size);
        }

        [[nodiscard]] uint32 index_from_handle(void* handle) {
            if (handle < handles || handle >= advance_ptr(handles, specs->total_handles_size)) {
                return region_chunk_allocator_null_value;
            }
            if (!is_aligned(handle, specs->handle_size)) {
                return region_chunk_allocator_null_value;
            }
            return divpow2(subptr(handle, handles), specs->handle_size_log2);
        }

        [[nodiscard]] void* handle_from_index(uint32 index) {
            if (index >= specs->num_handles) {
                return nullptr;
            }
            return advance_arr_log2(handles, specs->handle_size_log2, index);
        }

        [[nodiscard]] RegionChunkMemory region_data_to_memory_no_check(uint32 region, uint32 chunk, uint32 handle) {
            auto& region_specs = specs->region_specs[region];
            void* chunk_mem = advance_arr_log2(advance_ptr(regions, region_specs.region_offset), region_specs.chunk_size_log2, chunk);
            void* handle_mem = advance_arr_log2(handles, specs->handle_size_log2, handle);
            return {chunk_mem, handle_mem};
        }

        [[nodiscard]] RegionChunkMemory region_data_to_memory(uint32 region, uint32 chunk, uint32 handle) {
            CUW3_ALERT_RETURN_VAL(region >= specs->num_regions, {});
            CUW3_ALERT_RETURN_VAL(handle >= specs->num_handles, {});

            auto& region_specs = specs->region_specs[region];
            CUW3_ALERT_RETURN_VAL(region_specs.num_handles == 0, {});
            
            CUW3_ALERT_RETURN_VAL(chunk >= region_specs.num_handles, {});
            CUW3_ALERT_RETURN_VAL(!(region_specs.handle_offset <= handle && handle < region_specs.handle_offset + region_specs.num_handles), {});
            
            return region_data_to_memory_no_check(region, chunk, handle);
        }

        [[nodiscard]] uint32 search_suitable_region(uint64 size) {
            for (usize i = 0; i < specs->num_regions; i++) {
                if (size <= specs->chunk_size_search_sentinels[i]) {
                    return i;
                }
            }
            return region_chunk_allocator_null_value;
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


        // TODO : review this, this may be stupid, just decrease amount of rounds
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

        void deallocate_chunk(RegionChunkMemory memory) {
            if (!memory) {
                return;
            }
            if (auto allocation = ptr_to_allocation(memory.chunk)) {
                _deallocate_chunk(allocation);
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
    };
}