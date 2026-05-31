#pragma once

#include "conf.hpp"
#include "list.hpp"
#include "backoff.hpp"
#include "fast_arena.hpp"
#include "cuw3/atomic.hpp"
#include "retire_reclaim.hpp"
#include "thread_graveyard.hpp"
#include "region_chunk_handle.hpp"
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

    // NOTEs : this little buddy gets little bit fused with allocator.hpp itself
    // * probably it is a good idea just to move it into the allocator.hpp next to the main allocator data structure 
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
        FastArenaStepSplitAllocator step_split_allocator{};
        FastArenaSmallAllocator small_allocator{};

        // NOTE : see allocator.hpp for details. do something with it later.
        // this better be moved into the aux thread context
        // all this fields are in cuw3.cpp
        // kind of a workaround that will rest here for now
        uint64 thread_id{}; // for debug purposes mostly
        uint64 total_chunk_storage_size{};
        uint64 last_chunk_pool_split_id[conf_max_region_sizes] = {};
        uint64 last_graveyard_id{};
        uint64 extended_free_counter{};
    };
}