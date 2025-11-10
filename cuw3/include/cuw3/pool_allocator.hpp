#pragma once

#include "conf.hpp"
#include "cuw3/funcs.hpp"
#include "list.hpp"
#include "assert.hpp"
#include "retire_reclaim.hpp"
#include "region_chunk_handle.hpp"

namespace cuw3 {
    // TODO : NOTE about why you do we use this fuckery with cachelines (thread false sharing)

    // resource hierarchy
    // allocation -> pool shard -> pool shard pool -> region list -> root (thread)

    using PoolListEntry = DefaultListEntry;

    struct PoolShardHeader {
        uint32 next{};
    };

    struct PoolShardPool {
        struct alignas(conf_cacheline) {
            RegionChunkHandleHeader handle_header{};
            PoolListEntry list_entry{};

            struct {
                uint32 top{};
                uint32 head{};
                uint32 count{};
                uint32 capacity{};
            } shard_pool;

            uint32 pool_shard_size_log2{}; // constant, but let it exist as a field
            uint32 shard_pool_memory_size{}; // can also be determined externally but let it just rest here

            void* shard_pool_handles{}; // passed externally
            void* shard_pool_memory{}; // region chunk
        };

        struct alignas(conf_cacheline) {
            RetireReclaimEntry retire_reclaim_entry{};
            void* postponed_reclaimed{}; // list of handles
        };
    };

    static_assert(sizeof(PoolShardPool) <= conf_control_block_size, "pack struct field better or increase size of the control block");


    struct ChunkPoolHeader {
        uint32 next{};
    };

    // TODO : maybe I should include a reference to pool shard pool (it is retrieved )
    struct ChunkPool {
        struct alignas(conf_cacheline) {
            PoolListEntry list_entry{};

            struct {
                uint32 top{};
                uint32 head{};
                uint32 count{};
                uint32 capacity{};
            } chunk_pool;

            uint32 chunks_memory_size{};

            uint32 chunk_alignment{};
            uint32 chunk_size{};
            uint32 chunk_size_log2{};

            void* chunks_memory{};
        };

        struct alignas(conf_cacheline) {
            RetireReclaimEntry retire_reclaim_entry{};
            void* postponed_reclaimed{}; // list of chunks
        };
    };

    static_assert(sizeof(ChunkPool) <= conf_control_block_size, "pack struct field better or increase size of the control block");


    struct PoolShardPoolConfig {
        void* owner{};

        void* handle{};
        gsize handle_size{}; // initialized to conf_control_block_size

        void* shard_pool_memory{};
        gsize shard_pool_memory_size{};

        void* shard_pool_handles{};
        gsize shard_pool_handles_size{};

        gsize pool_shard_size{};
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

    struct PoolShardPoolView {
        [[nodiscard]] static PoolShardPoolView create(const PoolShardPoolConfig& config) {
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

            CUW3_ASSERT(is_aligned(config.shard_pool_memory, config.pool_shard_size), "invalid shard pool memory alignment");

            auto* pool = new (config.handle) PoolShardPool{};
            pool->shard_pool.top = 0;
            pool->shard_pool.count = 0;
            pool->shard_pool.capacity = expected_num_handles;
            pool->shard_pool.head = pool->shard_pool.capacity;

            pool->pool_shard_size_log2 = pool_shard_size_log2;
            pool->shard_pool_memory_size = config.shard_pool_memory_size;

            pool->shard_pool_handles = config.shard_pool_handles;
            pool->shard_pool_memory = config.shard_pool_memory;
            return {pool};
        }

        PoolShard _shard_from_index(uint32 index) {
            CUW3_ASSERT(index < pool->shard_pool.capacity, "invalid index of the shard");

            void* shard_handle = advance_arr_log2(pool->shard_pool_handles, conf_control_block_size_log2, index);
            void* shard_memory = advance_arr_log2(pool->shard_pool_memory, pool->pool_shard_size_log2, index);
            return {shard_handle, shard_memory};
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

        uint32 _index_from_shard(PoolShard shard) {
            CUW3_ASSERT(_valid_shard_handle(shard.shard_handle), "invalid shard handle");
            CUW3_ASSERT(_valid_shard_memory(shard.shard_memory), "invalid shard memory");

            return divpow2(subptr(shard.shard_memory, pool->shard_pool_handles), conf_control_block_size_log2);
        }

        bool _valid_pool_shard(PoolShard shard) {
            return shard && _valid_shard_handle(shard.shard_handle) && _valid_shard_memory(shard.shard_memory);
        }

        [[nodiscard]] PoolShard acquire() {
            if (pool->shard_pool.head != pool->shard_pool.capacity) {
                uint32 shard_index = pool->shard_pool.head;
                auto shard = _shard_from_index(shard_index);
                pool->shard_pool.head = ((PoolShardHeader*)shard.shard_handle)->next;
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

            ((PoolShardHeader*)shard.shard_handle)->next = pool->shard_pool.head;
            pool->shard_pool.head = _index_from_shard(shard);
            pool->shard_pool.count--;
        }

        bool empty() const {
            return pool->shard_pool.count == 0;
        }

        bool full() const {
            return pool->shard_pool.count == pool->shard_pool.capacity;
        }

        // TODO : retire-reclaim

        PoolShardPool* pool{};
    };


    struct ChunkPoolConfig {
        void* pool_handle{};
        gsize pool_handle_size{};

        void* pool_memory{};
        gsize pool_memory_size{};

        uint32 chunk_size{};
        uint32 chunk_alignment{};
    };

    struct ChunkPoolView {
        [[nodiscard]] static ChunkPoolView create(const ChunkPoolConfig& config) {
            // TODO : messages
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

            uint32 true_chunk_size = align(config.chunk_size, config.chunk_alignment);
            uint32 chunk_capacity = divchunk(config.pool_memory_size, true_chunk_size, pool->chunk_size_log2);
            pool->chunk_pool.capacity = chunk_capacity;
            pool->chunk_pool.head = pool->chunk_pool.capacity;
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

            ((ChunkPoolHeader*)chunk)->next = pool->chunk_pool.head;
            pool->chunk_pool.head = _chunk_to_index(chunk);
            pool->chunk_pool.count--;
        }

        bool empty() const {
            return pool->chunk_pool.count == 0;
        }

        bool full() const {
            return pool->chunk_pool.count == pool->chunk_pool.capacity;
        }

        // TODO : retire-reclaim

        ChunkPool* pool{};
    };
}