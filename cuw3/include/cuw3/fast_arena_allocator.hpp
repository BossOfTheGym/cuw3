#pragma once

#include "conf.hpp"
#include "list.hpp"
#include "backoff.hpp"
#include "retire_reclaim.hpp"
#include "region_chunk_handle.hpp"

namespace cuw3 {
    // TODO : some notes about operation

    using FastArenaListEntry = DefaultListEntry;

    // resource hierarchy
    // allocation -> arena -> (???)
    struct FastArena {
        static FastArena* list_entry_to_arena(FastArenaListEntry* list_entry) {
            return cuw3_field_to_obj(list_entry, FastArena, list_entry);
        }

        struct alignas(conf_cacheline) {
            RegionChunkHandleHeader region_chunk_header{};
            FastArenaListEntry list_entry{};

            uint64 freed{};
            uint64 top{};
            uint64 arena_memory_size{};

            uint64 arena_alignment{};

            void* arena_memory{};
        };

        struct alignas(conf_cacheline) {
            RetireReclaimEntry retire_reclaim_entry{};
        };
    };

    static_assert(sizeof(FastArena) <= conf_control_block_size, "pack struct field better or increase size of the control block");


    struct FastArenaConfig {
        void* owner{};

        void* arena_handle{};
        void* arena_memory{};

        uint32 arena_handle_size{};
        uint32 arena_alignment{};
        uint32 arena_memory_size{};

        RetireReclaimRawPtr retire_reclaim_flags{};
    };

    using FastArenaBackoff = SimpleBackoff;

    struct FastArenaView {
        [[nodiscard]] static FastArenaView create(const FastArenaConfig& config) {
            CUW3_ASSERT(config.owner, "owner was null");
            CUW3_ASSERT(config.arena_handle, "arena handle was null");
            CUW3_ASSERT(config.arena_memory, "arena memory was null");

            CUW3_ASSERT(config.arena_handle_size == conf_control_block_size, "invalid size of arena handle");
            CUW3_ASSERT(is_alignment(config.arena_alignment) && config.arena_alignment >= conf_min_alloc_alignment, "invalid alignment provided");
            CUW3_ASSERT(is_aligned(config.arena_memory_size, config.arena_alignment), "arena size is not properly aligned");
            CUW3_ASSERT(is_aligned(config.arena_memory, config.arena_alignment), "arena memory is not properly aligned");

            auto* arena = initz_region_chunk_handle<FastArena>(config.arena_handle, config.arena_handle_size);
            RegionChunkHandleHeaderView{&arena->region_chunk_header}.start_chunk_lifetime(config.owner, (uint64)RegionChunkType::FastArena);

            arena->arena_alignment = config.arena_alignment;
            arena->top = 0;
            arena->arena_memory_size = config.arena_memory_size;

            arena->arena_memory = config.arena_memory;

            (void)RetireReclaimEntryView::create(&arena->retire_reclaim_entry, config.retire_reclaim_flags, (uint32)RegionChunkType::FastArena, offsetof(FastArena, retire_reclaim_entry));
            return {arena};
        }


        [[nodiscard]] void* acquire(uint64 size) {
            CUW3_ASSERT(arena->arena_memory_size >= arena->top, "top is greater than memory size");

            uint64 old_top = arena->top;
            uint64 remaining = arena->arena_memory_size - arena->top;
            uint64 required_space = align(size, arena->arena_alignment);
            if (remaining < required_space) {
                return nullptr;
            }
            arena->top += required_space;
            return advance_ptr(arena->arena_memory, old_top);
        }

        void release(uint64 size) {
            uint64 alloc_freed = align(size, arena->arena_alignment);
            uint64 new_freed = arena->freed + alloc_freed;

            CUW3_CHECK(new_freed <= arena->arena_memory_size, "we have freed more than allocated");

            arena->freed = new_freed;
        }

        void reset() {
            arena->top = 0;
            arena->freed = 0;
        }

        bool empty() const {
            return arena->freed == arena->top;
        }

        uint64 remaining() const {
            CUW3_ASSERT(arena->arena_memory_size >= arena->top, "top is greater than memory size");

            return arena->arena_memory_size - arena->top;
        }

        uint64 fill_factor(uint64 part_size, uint64 part_size_log2) const {
            return divchunk(remaining() + part_size - 1, part_size, part_size_log2);
        }

        void retire_allocation(uint64 size) {
            auto retire_reclaim_entry_view = RetireReclaimPtrView{&arena->retire_reclaim_entry.head};
            RetireReclaimPtr old = retire_reclaim_entry_view.retire_data(size, FastArenaBackoff{});
        }

        void reclaim_allocations() {
            auto retire_reclaim_entry_view = RetireReclaimPtrView{&arena->retire_reclaim_entry.head};
            RetireReclaimPtr reclaimed = retire_reclaim_entry_view.reclaim();
            release(reclaimed.value_shifted());
        }


        FastArena* arena{};
    };
}