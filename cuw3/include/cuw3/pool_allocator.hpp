#pragma once

#include "list.hpp"
#include "conf.hpp"
#include "funcs.hpp"
#include "assert.hpp"
#include "backoff.hpp"
#include "retire_reclaim.hpp"
#include "region_chunk_handle.hpp"

namespace cuw3 {
    // resource hierarchy
    // allocation -> pool shard -> pool shard pool -> (???)

    using PoolShardPoolListEntry = DefaultListEntry;

    struct PoolShardPoolHandleHeader {
        uint32 next{};
    };

    // TODO : 'pool shard pool' sounds fucking retarded
    // I wanted it to mean something like 'pool of chunk pools'
    // maybe I should name it something like that... or maybe just shard pool

    // NOTE: shard memory can be restored using shard handle
    struct alignas(conf_cacheline) PoolShardPool {
        static PoolShardPool* list_entry_to_shard_pool(PoolShardPoolListEntry* list_entry) {
            return cuw3_field_to_obj(list_entry, PoolShardPool, list_entry);
        }

        // cacheline 0
        RegionChunkHandleHeader region_chunk_header{};
        PoolShardPoolListEntry list_entry{};

        struct {
            uint32 top{};
            uint32 head{};
            uint32 count{};
            uint32 capacity{};
        } shard_pool;

        uint32 pool_shard_size_log2{};
        uint32 shard_pool_memory_size{}; // can also be determined externally but let it just rest here

        alignas(8) void* shard_pool_handles{}; // passed externally
        alignas(8) void* shard_pool_memory{}; // region chunk

        // cacheline 1
        RetireReclaimEntry retire_reclaim_entry{};
        uint64 pad1[4] = {};
    };

    static_assert(sizeof(PoolShardPool) <= conf_control_block_size, "pack struct field better or increase size of the control block");


    using ChunkPoolListEntry = DefaultListEntry; 

    struct ChunkPoolHeader {
        union {
            uint32 next;
            void* next_retired{};
        };
    };

    static_assert(sizeof(ChunkPoolHeader) <= conf_min_alloc_size, "we cannot guarantee enough space for chunk to be reired");

    // chunk pool is subresource of shard pool, we dont need to store reference to the shard pool here
    struct alignas(conf_cacheline) ChunkPool {
        static ChunkPool* list_entry_to_chunk_pool(ChunkPoolListEntry* list_entry) {
            return cuw3_field_to_obj(list_entry, ChunkPool, list_entry);
        }

        // cacheline 0
        ChunkPoolListEntry list_entry{};

        struct {
            uint32 top{};
            uint32 head{};
            uint32 count{};
            uint32 capacity{};
        } chunk_pool;

        uint32 bin_index{};
        uint32 pad{};

        uint32 chunks_memory_size{};
        uint32 chunk_alignment{};
        uint32 chunk_size{};
        uint32 chunk_size_log2{};

        alignas(8) void* chunks_memory{};

        // cacheline 1
        RetireReclaimEntry retire_reclaim_entry{};
        uint64 pad1[4] = {};
    };

    static_assert(sizeof(ChunkPool) <= conf_control_block_size, "pack struct field better or increase size of the control block");


    struct PoolShardPoolConfig {
        void* owner{};

        void* handle{};
        gsize handle_size{}; // must be initialized to conf_control_block_size

        void* shard_pool_memory{};
        gsize shard_pool_memory_size{};

        void* shard_pool_handles{};
        gsize shard_pool_handles_size{};

        gsize pool_shard_size{};

        RetireReclaimRawPtr retire_reclaim_flags{};
    };

    struct PoolShard {
        bool empty() const {
            return !(shard_handle && shard_memory);
        }

        explicit operator bool() const {
            return !empty();
        }

        void* shard_handle{};
        void* shard_memory{};
    };

    using PoolShardPoolBackoff = SimpleBackoff;

    // NOTE : shards are used by pools only which is fine. But if we were to reuse this with different underlying memory-management data-structure.
    // we would require some more advanced approach (at least we would have to use offset and type fields of the retire-reclaim entry)
    // location of shard_memory can be restored from the location of shard_handle
    struct PoolShardPoolView {
        [[nodiscard]] static PoolShardPoolView create(const PoolShardPoolConfig& config) {
            // TODO : maybe insert an alignment assert here?
            CUW3_ASSERT(config.owner, "owner is null");
            CUW3_ASSERT(config.handle, "handle is null");
            CUW3_ASSERT(config.shard_pool_memory, "shard pool memory is null");
            CUW3_ASSERT(config.shard_pool_handles, "shard pool handles is null");

            CUW3_ASSERT(is_aligned(config.handle, conf_cacheline), "insufficient alignment for handle memory");
            CUW3_ASSERT(config.handle_size == conf_control_block_size, "invalid size for control block");
            
            CUW3_ASSERT(is_pow2(config.pool_shard_size), "pool shard is not power of 2");
            
            uint32 pool_shard_size_log2 = intlog2(config.pool_shard_size);
            uint32 expected_num_handles = divpow2(config.shard_pool_memory_size, pool_shard_size_log2);
            uint32 given_num_handles = config.shard_pool_handles_size / conf_control_block_size;

            CUW3_ASSERT(is_aligned(config.shard_pool_handles, conf_cacheline), "insufficient alignment for shard pool handles");
            CUW3_ASSERT(given_num_handles >= expected_num_handles, "insufficient space for handles was provided");

            auto* pool = initz_region_chunk_handle<PoolShardPool>(config.handle, config.handle_size);
            RegionChunkHandleHeaderView{&pool->region_chunk_header}.start_chunk_lifetime(config.owner, (uint64)RegionChunkType::PoolShardPool);

            pool->shard_pool.top = 0;
            pool->shard_pool.count = 0;
            pool->shard_pool.capacity = expected_num_handles;
            pool->shard_pool.head = pool->shard_pool.capacity;

            pool->pool_shard_size_log2 = pool_shard_size_log2;
            pool->shard_pool_memory_size = config.shard_pool_memory_size;

            pool->shard_pool_handles = config.shard_pool_handles;
            pool->shard_pool_memory = config.shard_pool_memory;

            (void)RetireReclaimEntryView::create(&pool->retire_reclaim_entry, config.retire_reclaim_flags, (uint32)RegionChunkType::PoolShardPool, offsetof(PoolShardPool, retire_reclaim_entry));
            return {pool};
        }


        void* _shard_handle_from_index(uint32 index) {
            CUW3_ASSERT(index < pool->shard_pool.capacity, "invalid index of the shard");

            return advance_arr_log2(pool->shard_pool_handles, conf_control_block_size_log2, index);
        }

        void* _shard_memory_from_index(uint32 index) {
            CUW3_ASSERT(index < pool->shard_pool.capacity, "invalid index of the shard");

            return advance_arr_log2(pool->shard_pool_memory, pool->pool_shard_size_log2, index);
        }

        PoolShard _shard_from_index(uint32 index) {
            CUW3_ASSERT(index < pool->shard_pool.capacity, "invalid index of the shard");

            return {_shard_handle_from_index(index), _shard_memory_from_index(index)};
        }

        bool _valid_shard_handle(void* shard_handle) {
            return shard_handle
                && (uintptr)pool->shard_pool_handles <= (uintptr)shard_handle
                && subptr(shard_handle, pool->shard_pool_handles) < pool->shard_pool.capacity * conf_control_block_size
                && is_aligned(shard_handle, conf_cacheline);
        }

        bool _valid_shard_memory(void* shard_memory) {
            return shard_memory
                && (uintptr)pool->shard_pool_memory <= (uintptr)shard_memory
                && subptr(shard_memory, pool->shard_pool_memory) < pool->shard_pool_memory_size
                && is_aligned(shard_memory, intpow2(pool->pool_shard_size_log2));
        }

        uint32 _index_from_shard_handle(void* shard_handle) {
            CUW3_ASSERT(_valid_shard_handle(shard_handle), "invalid shard handle");

            return divpow2(subptr(shard_handle, pool->shard_pool_handles), conf_control_block_size_log2);
        }

        uint32 _index_from_shard_memory(void* shard_memory) {
            CUW3_ASSERT(_valid_shard_memory(shard_memory), "invalid shard memory");

            return divpow2(subptr(shard_memory, pool->shard_pool_memory), pool->pool_shard_size_log2);
        }

        uint32 _index_from_shard(PoolShard shard) {
            return _index_from_shard_handle(shard.shard_handle);
        }

        bool _valid_pool_shard(PoolShard shard) {
            return _valid_shard_handle(shard.shard_handle) && _valid_shard_memory(shard.shard_memory);
        }


        void* owner() const {
            // non-atomic, but that's okay, nobody should be able to modify this location
            return pool->region_chunk_header.data.ptr();
        }

        [[nodiscard]] PoolShard acquire() {
            if (pool->shard_pool.head != pool->shard_pool.capacity) {
                uint32 shard_index = pool->shard_pool.head;
                auto shard = _shard_from_index(shard_index);
                pool->shard_pool.head = ((PoolShardPoolHandleHeader*)shard.shard_handle)->next;
                pool->shard_pool.count++;
                return shard;
            }

            if (pool->shard_pool.top < pool->shard_pool.capacity) {
                auto shard = _shard_from_index(pool->shard_pool.top++);
                pool->shard_pool.count++;
                return shard;
            }

            return {};
        }

        void release(PoolShard shard) {
            CUW3_ASSERT(_valid_pool_shard(shard), "what kind of shit are you trying to feed me!");

            new (shard.shard_handle) PoolShardPoolHandleHeader{ .next = pool->shard_pool.head };
            pool->shard_pool.head = _index_from_shard(shard);
            pool->shard_pool.count--;
        }

        bool empty() const {
            return pool->shard_pool.count == 0;
        }

        bool full() const {
            return pool->shard_pool.count == pool->shard_pool.capacity;
        }


        struct RetireChunkPoolOps {
            void set_next(void* resource, void* head) {
                ((RetireReclaimEntry*)resource)->next = head;
            }
        };

        [[nodiscard]] RetireReclaimPtr retire_pool(RetireReclaimEntry* retired_entry) {
            auto retire_reclaim_entry_view = RetireReclaimPtrView{&pool->retire_reclaim_entry.head};
            return retire_reclaim_entry_view.retire_ptr(retired_entry, PoolShardPoolBackoff{}, RetireChunkPoolOps{});
        }

        [[nodiscard]] RetireReclaimPtr reclaim_pools() {
            auto retire_reclaim_entry_view = RetireReclaimPtrView{&pool->retire_reclaim_entry.head};
            return retire_reclaim_entry_view.reclaim();
        }

        void postpone_entry(RetireReclaimEntry* retired_entry) {
            CUW3_ASSERT(!pool->retire_reclaim_entry.next_postponed, "already postponed, invalid case");

            pool->retire_reclaim_entry.next_postponed = retired_entry;
        }

        [[nodiscard]] RetireReclaimEntry* reclaim_postponed_entries() {
            return (RetireReclaimEntry*)std::exchange(pool->retire_reclaim_entry.next_postponed, nullptr);
        }


        PoolShardPool* pool{};
    };


    struct ChunkPoolConfig {
        void* pool_handle{};
        gsize pool_handle_size{};

        void* pool_memory{};
        gsize pool_memory_size{};

        uint32 chunk_size{};
        uint32 chunk_alignment{};
        uint32 bin_index{}; // little hint facilitating chunk pool bin location, intrusive but useful TODO: ?????

        RetireReclaimRawPtr retire_reclaim_flags{};
    };

    using ChunkPoolBackoff = SimpleBackoff;

    struct ChunkPoolView {
        [[nodiscard]] static ChunkPoolView create(const ChunkPoolConfig& config) {
            CUW3_ASSERT(config.pool_handle, "pool handle is null");
            CUW3_ASSERT(config.pool_memory, "pool memory is null");
            CUW3_ASSERT(config.pool_handle_size == conf_control_block_size, "invalid size of pool handle memory");
            CUW3_ASSERT(config.pool_memory_size >= config.chunk_size, "pool must contain at least one chunk");
            
            CUW3_ASSERT(is_pow2(config.pool_memory_size), "chunk pool memory size is not power of 2");
            CUW3_ASSERT(is_alignment(config.chunk_alignment), "invalid chunk alignment");
            CUW3_ASSERT(is_aligned(config.pool_memory, config.chunk_alignment), "chunk memory must be aligned to chunk alignment");
            CUW3_ASSERT(config.chunk_size >= conf_min_alloc_size, "too small chunk size");

            auto* pool = new (config.pool_handle) ChunkPool{};
            pool->chunks_memory_size = config.pool_memory_size;
            pool->chunk_size_log2 = is_pow2(config.chunk_size) ? intlog2(config.chunk_size) : 0;
            pool->chunk_size = config.chunk_size;
            pool->chunk_alignment = config.chunk_alignment;
            pool->bin_index = config.bin_index;

            uint32 true_chunk_size = align(config.chunk_size, config.chunk_alignment);
            uint32 chunk_capacity = divchunk(config.pool_memory_size, true_chunk_size, pool->chunk_size_log2);
            pool->chunk_pool.capacity = chunk_capacity;
            pool->chunk_pool.head = pool->chunk_pool.capacity;

            // type can be null here as there are no other entry types
            (void)RetireReclaimEntryView::create(&pool->retire_reclaim_entry, config.retire_reclaim_flags, 0, offsetof(ChunkPool, retire_reclaim_entry));
            return {pool};
        }

        void* _index_to_chunk(uint32 index) {
            CUW3_ASSERT(index < pool->chunk_pool.capacity, "invalid chunk index");

            return advance_chunk(pool->chunks_memory, pool->chunk_size, pool->chunk_size_log2, index);
        }

        uint32 _chunk_to_index(void* chunk) {
            CUW3_ASSERT(_valid_chunk(chunk), "invalid chunk provided");

            return divchunk(subptr(chunk, pool->chunks_memory), pool->chunk_size, pool->chunk_size_log2);
        }

        bool _valid_chunk(void* chunk) {
            return chunk
                && (uintptr)pool->chunks_memory <= (uintptr)chunk
                && subptr(chunk, pool->chunks_memory) < pool->chunks_memory_size
                && is_aligned(chunk, pool->chunk_alignment);
        }


        [[nodiscard]] void* acquire() {
            if (pool->chunk_pool.head != pool->chunk_pool.capacity) {
                uint32 chunk_index = pool->chunk_pool.head;
                void* chunk = _index_to_chunk(chunk_index);
                pool->chunk_pool.head = ((ChunkPoolHeader*)chunk)->next;
                pool->chunk_pool.count++;
                return chunk;
            }

            if (pool->chunk_pool.top < pool->chunk_pool.capacity) {
                void* chunk = _index_to_chunk(pool->chunk_pool.top);
                pool->chunk_pool.count++;
                return chunk;
            }

            return nullptr;
        }

        void release(void* chunk) {
            CUW3_ASSERT(_valid_chunk(chunk), "what the fuck are you trying to feed me?!!");

            new (chunk) ChunkPoolHeader{{.next = pool->chunk_pool.head}};
            pool->chunk_pool.head = _chunk_to_index(chunk);
            pool->chunk_pool.count--;
        }

        bool empty() const {
            return pool->chunk_pool.count == 0;
        }

        bool full() const {
            return pool->chunk_pool.count == pool->chunk_pool.capacity;
        }


        struct RetireChunkOps {
            void set_next(void* resource, void* head) {
                ((ChunkPoolHeader*)resource)->next_retired = head;
            }
        };

        [[nodiscard]] RetireReclaimPtr retire_chunk(void* chunk) {
            new (chunk) ChunkPoolHeader{{.next_retired = nullptr}};

            auto retire_reclaim_entry_view = RetireReclaimPtrView{&pool->retire_reclaim_entry.head};
            return retire_reclaim_entry_view.retire_ptr(chunk, ChunkPoolBackoff{}, RetireChunkOps{});
        }

        [[nodiscard]] RetireReclaimPtr reclaim_chunks() {
            auto retire_reclaim_entry_view = RetireReclaimPtrView{&pool->retire_reclaim_entry.head};
            return retire_reclaim_entry_view.reclaim();
        }

        void postpone_chunk(void* chunk) {
            CUW3_ASSERT(!pool->retire_reclaim_entry.next_postponed, "invalid case, something has already been postponed");

            pool->retire_reclaim_entry.next_postponed = chunk;
        }

        [[nodiscard]] ChunkPoolHeader* reclaim_postponed_chunks() {
            return (ChunkPoolHeader*)std::exchange(pool->retire_reclaim_entry.next_postponed, nullptr);
        }


        ChunkPool* pool{};
    };


}