#pragma once

#include "conf.hpp"
#include "list.hpp"
#include "bitmap.hpp"
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
        [[nodiscard]] static FastArena* create_fast_arena(const FastArenaConfig& config) {
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
            return arena;
        }

        [[nodiscard]] static FastArenaView create(const FastArenaConfig& config) {
            return {create_fast_arena(config)};
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


    struct FastArenaAllocatorConfig {
        uint64 min_arena_size{};
        uint64 max_arena_size{};

        uint64 min_lookup_bin_pow2{};
        uint64 max_lookup_bin_pow2{};
    };

    struct FastArenaAllocation {

    };

    struct FastArenaBins {
        using FastArenaBinListEntry = DefaultListEntry;
        using FastArenaBinListOps = DefaultListOps<FastArenaBinListEntry>;

        struct FastArenaBin {
            FastArenaBinListEntry list_head{};
        };

        static constexpr gsize align_axis = conf_max_fast_arenas;
        static constexpr gsize lookup_step_axis = conf_max_fast_arena_lookup_steps;
        static constexpr gsize lookup_split_axis = conf_max_fast_arena_lookup_split;
        
        using FastArenaBitmap = Bitmap<gsize, lookup_split_axis>;

        struct FastArenaLocation {
            uint32 step{};
            uint32 split{};
        };

        uint64 locate_alignment(uint64 alignment) {
            uint64 alignment_log2 = intlog2(alignment);
            CUW3_ASSERT(alignment_log2 <= max_arena_alignment_log2, "cannot satisfy alignment request");

            alignment_log2 = std::max(alignment_log2, min_arena_alignment_log2);
            return alignment_log2 - min_arena_alignment_log2;
        }

        // size must be aligned beforehand if required
        FastArenaLocation locate_arena_bin(uint64 size) {
            uint64 step_size_log2 = std::min(intlog2(size), max_arena_step_size_log2);

            uint64 step = 0;
            uint64 split_size_base = 0;
            uint64 split_size_log2 = 0;
            if (step_size_log2 < min_arena_step_size_log2) {
                step = 0;
                split_size_base = 0;
                split_size_log2 = min_arena_step_size_log2;
            } else {
                step = step_size_log2 - min_arena_step_size_log2 + 1;
                split_size_base = intpow2(step_size_log2);
                split_size_log2 = step_size_log2;
            }

            uint64 split = std::min(divpow2(mulpow2(size - split_size_base, num_splits_log2), split_size_log2), num_splits);
            return {(uint32)step, (uint32)split,};
        }

        FastArenaLocation locate_empty_arena_bin() {
            return {};
        }

        FastArenaLocation locate_last_bin() {
            return {(uint32)num_steps, (uint32)num_splits};
        }

        void add_arena(const FastArenaConfig& config, uint64 alignment) {
            uint64 alignment_log2 = intlog2(alignment);

            auto fast_arena = FastArenaView::create_fast_arena(config);
            // TODO
        }

        FastArenaBin fast_arena_bins[align_axis][lookup_step_axis + 1][lookup_split_axis + 1] = {};
        FastArenaBitmap fast_arena_bitmaps[align_axis][lookup_step_axis + 1] = {};

        FastArenaBin free_arenas[align_axis] = {};
        FastArena* curr_arena[align_axis] = {};

        uint64 num_alignments{};
        uint64 num_steps{};
        uint64 num_splits{};
        uint64 num_splits_log2{};

        //uint64 min_arena_step_size{};
        uint64 min_arena_step_size_log2{};
        //uint64 max_arena_step_size{};
        uint64 max_arena_step_size_log2{};

        uint64 min_arena_alignment_log2{};
        uint64 max_arena_alignment_log2{};
    };

    struct FastArenaAllocator {
        struct alignas(conf_cacheline) RetiredArenasRoot {
            RetireReclaimEntry entry{};
        };

        RetiredArenasRoot retired_arenas_root{};
        FastArenaBins fast_arena_bins{};
        FastArena* curr_arena{};
    };
}