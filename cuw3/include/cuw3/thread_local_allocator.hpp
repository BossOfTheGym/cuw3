#pragma once

#include "conf.hpp"
#include "list.hpp"
#include "backoff.hpp"
#include "fast_arena.hpp"
#include "cuw3/atomic.hpp"
#include "retire_reclaim.hpp"
#include "thread_graveyard.hpp"
#include "region_chunk_handle.hpp"
#include "region_chunk_allocator.hpp"
#include "fast_arena_small_allocator.hpp"
#include "fast_arena_step_split_allocator.hpp"

namespace cuw3 {
    struct ThreadLocalAllocatorConfig {
        FastArenaStepSplitAllocatorConfig step_split_alloc_config{};
        FastArenaSmallAllocatorConfig small_alloc_config{};
        uint64 thread_id{};
    };

    using ThreadGraveyardEntry = DefaultThreadGraveyardEntry;
    using ThreadGraveyardOps = DefaultThreadGraveyardOps;

    // thread local allocator: core structure that holds context of all allocator types 
    // this type is not relocatable: its address must remain constant during lifetime
    struct alignas(region_owner_alignment) ThreadLocalAllocator {
        static ThreadLocalAllocator* graveyard_entry_to_allocator(ThreadGraveyardEntry* entry) {
            return cuw3_field_to_obj(entry, ThreadLocalAllocator, graveyard_entry);
        }

        [[nodiscard]] static ThreadLocalAllocator* create(Memory memory, const ThreadLocalAllocatorConfig& config) {
            CUW3_CHECK_RETURN_VAL(memory.fits<ThreadLocalAllocator>(), nullptr, "invalid memory");

            auto* tla = new (memory.get()) ThreadLocalAllocator{};
            auto* step_split_allocator = FastArenaStepSplitAllocator::create(Memory::from(&tla->step_split_allocator), config.step_split_alloc_config);
            CUW3_CHECK_RETURN_VAL(step_split_allocator, nullptr, "failed to create step_split_allocator");

            auto* small_allocator = FastArenaSmallAllocator::create(Memory::from(&tla->small_allocator), config.small_alloc_config);
            CUW3_CHECK_RETURN_VAL(small_allocator, nullptr, "failed to create small_allocator");

            for (uint i = 0; i < conf_max_region_sizes; i++) {
                tla->cached_chunks[i] = null_region_chunk_allocation;
            }

            tla->thread_id = config.thread_id;

            return tla;
        }

        // just to be sure that it is consistent with ThreadGraveyardOps
        ThreadGraveyardEntry* graveyard_entry_ptr() {
            return &graveyard_entry;
        }

        bool empty() const {
            return step_split_allocator.empty() && small_allocator.empty();
        }

        ThreadGraveyardEntry graveyard_entry{};

        RegionChunkAllocation cached_chunks[conf_max_region_sizes] = {};
        uint64 total_chunk_storage_size{};
        uint64 last_chunk_pool_split_id[conf_max_region_sizes] = {};
        uint64 last_graveyard_id{};
        uint64 this_cleanup_counter{};
        uint64 grave_cleanup_counter{};
        
        uint64 thread_id{}; // for debug purposes mostly

        FastArenaStepSplitAllocator step_split_allocator{};
        FastArenaSmallAllocator small_allocator{};
    };
}