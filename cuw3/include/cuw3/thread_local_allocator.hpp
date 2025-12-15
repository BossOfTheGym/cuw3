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


        static void _init_fast_arenas(ThreadLocalAllocator* allocator) {
            auto retire_flag = (RetireReclaimRawPtr)RetireReclaimFlags::RetiredFlag;
            (void)RetireReclaimEntryView::create(&allocator->retired_fast_arenas.entry, retire_flag);

            uint align_axis = conf_thread_local_allocator_max_fast_arenas;
            uint split_axis = conf_fast_arena_remaining_split + 1;
            for (uint i = 0; i < align_axis; i++) {
                for (uint j = 0; j < split_axis; j++) {
                    list_init(&allocator->fast_arena_bins[i][j].list_head, ThreadFastArenaBinListOps{});
                }
            }
            for (uint i = 0; i < align_axis; i++) {
                allocator->fast_arena_split_free_mask[i] = 0;
            }
            allocator->fast_arena_alignment_free_mask = 0;
        }

        static void _init_pools(ThreadLocalAllocator* allocator) {
            auto retire_flag = (RetireReclaimRawPtr)RetireReclaimFlags::RetiredFlag;
            (void)RetireReclaimEntryView::create(&allocator->retired_pools.entry, retire_flag);

            uint max_regions = conf_thread_local_allocator_max_regions;
            for (uint i = 0; i < max_regions; i++) {
                auto& bin = allocator->shard_pool_bins[i];
                list_init(&bin.free_list_head, ThreadPoolBinListOps{});
                list_init(&bin.full_list_head, ThreadPoolBinListOps{});
            }

            uint max_chunks = conf_thread_local_allocator_num_chunk_pools;
            for (uint i = 0; i < max_chunks; i++) {
                auto& bin = allocator->chunk_pool_bins[i];
                list_init(&bin.free_list_head, ThreadPoolBinListOps{});
                list_init(&bin.full_list_head, ThreadPoolBinListOps{});
            }

            allocator->shard_pool_bin_free_mask = 0;
            allocator->chunk_pool_bin_free_mask = 0;
        }

        static void _init_allocators(ThreadLocalAllocator* allocator) {
            _init_fast_arenas(allocator);
            _init_pools(allocator);
        }

        [[nodiscard]] static ThreadLocalAllocator* init(const ThreadLocalAllocatorConfig& config) {
            // TODO
            CUW3_ASSERT(config.allocator_memory, "");
            CUW3_ASSERT(config.allocator_memory_size >= sizeof(ThreadLocalAllocator), "");
            
            // TODO : assertions

            auto* allocator = new (config.allocator_memory) ThreadLocalAllocator{};
            allocator->allocator_memory_size = config.allocator_memory_size;

            allocator->num_regions = config.num_regions;

            allocator->min_fast_arena_size = config.min_fast_arena_size;
            allocator->max_fast_arena_size = config.max_fast_arena_size;
            allocator->min_fast_arena_alignment = config.min_fast_arena_alignment;
            allocator->max_fast_arena_alignment = config.max_fast_arena_alignment;
            
            allocator->num_chunk_pool_bins = config.max_chunk_pow2 - config.min_chunk_pow2 + 1;
            allocator->shard_pool_size = config.shard_pool_size;
            allocator->min_chunk_pow2 = config.min_chunk_pow2;
            allocator->max_chunk_pow2 = config.max_chunk_pow2;

            _init_allocators(allocator);
            return allocator;
        }

        [[nodiscard]] static ThreadLocalAllocator* initz(const ThreadLocalAllocatorConfig& config) {
            // TODO
            CUW3_ASSERT(config.allocator_memory, "");
            CUW3_ASSERT(config.allocator_memory_size >= sizeof(ThreadLocalAllocator), "");

            memset(config.allocator_memory, 0x00, config.allocator_memory_size);
            return init(config);
        }

        static ThreadLocalAllocator* graveyard_entry_to_allocator(ThreadGraveyardEntry* entry) {
            return cuw3_field_to_obj(entry, ThreadLocalAllocator, graveyard_entry);
        }


        // impl
        // TODO

        // API
        // region_index is retrieved externally when we locate allocator that owns allocation
        uint get_arena_split(FastArena* arena) const {

        }

        void add_fast_arena(const FastArenaConfig& config, uint64 region_index) {
            auto fast_arena = FastArenaView::create(config);

            uint arena_split
            fast_arena_bins[region_index][]
        }

        void* fast_arena_allocate(uint64 size, uint64 alignment) {

        }

        void fast_arena_deallocate(FastArena* arena, uint64 region_index, uint64 size) {

        }

        void retire_fast_arena_deallocation(FastArena* arena, uint64 region_index, uint64 size) {

        }

        void reclaim_fast_arena_deallocations() {

        }
        

        void add_shard_pool(const PoolShardPoolConfig& config, uint64 region_index) {

        }

        void* pool_allocate(uint64 size, uint64 alignment) {

        }

        void* pool_deallocate(PoolShardPool* shard_pool, uint64 region_index, ChunkPool* pool, void* ptr) {

        }

        void retire_pool_deallocation() {

        }

        void reclaim_pool_deallocations() {

        }

        
        ThreadGraveyardEntry graveyard_entry{};

        // region chunks to be recycled
        RecycledRegionChunkBin recycled_region_chunk_bins[conf_thread_local_allocator_max_regions] = {};

        // NOTE : here we can move all the allocators into the separate types 
        // allocators
        // fast_arena retire path (fast_arena_allocator.hpp):
        //   (allocation -> arena) -> retired_fast_arenas
        ThreadRetiredResource retired_fast_arenas{};
        ThreadFastArenaBin fast_arena_bins[conf_thread_local_allocator_max_fast_arenas][conf_fast_arena_remaining_split + 1] = {};
        uint64 fast_arena_split_free_mask[conf_thread_local_allocator_max_fast_arenas] = {};
        uint64 fast_arena_alignment_free_mask{};
        //        
        // pool retire path (pool_allocator.hpp):
        //   (allocation -> pool shard -> pool shard pool) -> retired_pools
        ThreadRetiredResource retired_pools{};
        ThreadPoolShardPoolBin shard_pool_bins[conf_thread_local_allocator_max_regions] = {};
        ThreadChunkPoolBin chunk_pool_bins[conf_thread_local_allocator_num_chunk_pools] = {};
        uint64 shard_pool_bin_free_mask{};
        uint64 chunk_pool_bin_free_mask{};

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