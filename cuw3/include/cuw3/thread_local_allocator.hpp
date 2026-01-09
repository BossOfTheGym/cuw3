#pragma once

#include "conf.hpp"
#include "cuw3/thread_graveyard.hpp"
#include "list.hpp"
#include "backoff.hpp"
#include "cuw3/atomic.hpp"
#include "retire_reclaim.hpp"
#include "pool_allocator.hpp"
#include "region_chunk_handle.hpp"
#include "fast_arena_allocator.hpp"

namespace cuw3 {
    struct ThreadLocalAllocatorConfig {
        void* allocator_memory{};
        uint64 allocator_memory_size{};

        uint64 num_regions{};

        uint64 min_fast_arena_size{};
        uint64 max_fast_arena_size{};
        uint64 min_fast_arena_alignment{};
        uint64 max_fast_arena_alignment{};
        
        uint64 shard_pool_size{};
        uint64 min_chunk_pow2{};
        uint64 max_chunk_pow2{};
    };

    // thread local allocator: core structure that holds context of all allocator types 
    // this type is not relocatable: its address must remain constant during lifetime
    // many parts of an allocator system rely on that including some internal fields of the thread allocator itself (retire-reclaim system)
    // NOTE: inconsistency: has fixed amount of arenas and pools when  
    struct ThreadLocalAllocator {
        using ThreadGraveyardEntry = DefaultThreadGraveyardEntry;
        using ThreadGraveyardOps = DefaultThreadGraveyardOps;

        struct alignas(conf_cacheline) ThreadRetiredResource {
            RetireReclaimEntry entry{};
        };

        using ThreadFastArenaBinListEntry = DefaultListEntry;
        using ThreadFastArenaBinListOps = DefaultListOps<ThreadFastArenaBinListEntry>;

        struct ThreadFastArenaBin {
            ThreadFastArenaBinListEntry list_head{};
        };
        
        using ThreadPoolBinListEntry = DefaultListEntry;
        using ThreadPoolBinListOps = DefaultListOps<ThreadPoolBinListEntry>;

        struct ThreadPoolBin {
            ThreadPoolBinListEntry free_list_head{}; // has free resource
            ThreadPoolBinListEntry full_list_head{}; // has on free resource
        };

        using ThreadPoolShardPoolBin = ThreadPoolBin;
        using ThreadChunkPoolBin = ThreadPoolBin;

        struct RecycledRegionChunkBin {
            RegionChunkHandle* head{};
        };


        static ThreadLocalAllocator* graveyard_entry_to_allocator(ThreadGraveyardEntry* entry) {
            return cuw3_field_to_obj(entry, ThreadLocalAllocator, graveyard_entry);
        }


        // impl
        // TODO

        // API
        // void add_shard_pool(const PoolShardPoolConfig& config, uint64 region_index) {

        // }

        // void* pool_allocate(uint64 size, uint64 alignment) {

        // }

        // void* pool_deallocate(PoolShardPool* shard_pool, uint64 region_index, ChunkPool* pool, void* ptr) {

        // }

        // void retire_pool_deallocation() {

        // }

        // void reclaim_pool_deallocations() {

        // }

        
        ThreadGraveyardEntry graveyard_entry{};

        // NOTE : some of this is not exactly 'common' but can be attached to either of allocators
        // common stuff
        uint64 allocator_memory_size{};

        uint64 num_regions{};
        uint64 num_fast_arena_bins{};

        uint64 min_fast_arena_size{};
        uint64 max_fast_arena_size{};
        uint64 min_fast_arena_alignment{};
        uint64 max_fast_arena_alignment{};

        uint64 shard_pool_size{};
        uint64 min_chunk_pow2{};
        uint64 max_chunk_pow2{};
        uint64 num_chunk_pool_bins{};
    };
}