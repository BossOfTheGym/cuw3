#pragma once

#include "conf.hpp"
#include "list.hpp"
#include "utils.hpp"
#include "bitmap.hpp"
#include "assert.hpp"
#include "backoff.hpp"
#include "fast_arena.hpp"
#include "retire_reclaim.hpp"
#include "region_chunk_handle.hpp"

namespace cuw3 {
    struct FastArenaSmallBinConfig {
        uint64 alignment{};
        uint64 size_cutoff{};
    };

    // current arena is always filled
    // free_list contains arenas that have more than size_curoff space remaining
    // cutoff_list contains arenas that have less than size_ctoff space remaining
    // recyled_list contains arenas that have less than size_cutoff space remaining (we cannot allocate from them)
    struct FastArenaSmallBin {
        [[nodiscard]] static FastArenaSmallBin* create(Memory memory, const FastArenaSmallBinConfig& config) {
            CUW3_CHECK_RETURN_VAL(memory.fits<FastArenaSmallBin>(), nullptr, "invalid memory");

            auto* bin = new (memory.get()) FastArenaSmallBin{};
            list_init(&bin->free_list, FastArenaListOps{});
            list_init(&bin->cutoff_list, FastArenaListOps{});
            list_init(&bin->recycled_list, FastArenaListOps{});
            bin->alignment = config.alignment;
            bin->size_cutoff = config.size_cutoff;
            return bin;
        }

        [[nodiscard]] AcquiredTypedResource<FastArena> acquire(uint64 size) {
            // firstly, see if can allocate from the current_arena
            if (current_arena) {
                auto arena_view = FastArenaView{current_arena};
                if (arena_view.can_allocate(size)) {
                    return AcquiredTypedResource<FastArena>::acquired(
                        std::exchange(current_arena, nullptr)
                    );
                }
                // if we cannot, then define if it is worth keeping it
                if (size > 2 * arena_view.remaining()) {
                    list_push_head(&recycled_list, &current_arena->list_entry, FastArenaListOps{});
                    current_arena = nullptr;
                }
            }

            // secondly, see if we anything can be taken from the free_list
            if (!list_empty(&free_list, FastArenaListOps{})) {
                auto* arena = FastArena::list_entry_to_arena(list_pop_head(&free_list, FastArenaListOps{}));
                return AcquiredTypedResource<FastArena>::acquired(arena);
            }

            // nothing was in the free_list, check cutoff_list
            if (!list_empty(&cutoff_list, FastArenaListOps{})) {
                // we can attempt to allocate from the first bin
                auto* arena = FastArena::list_entry_to_arena(list_next(&cutoff_list, FastArenaListOps{}));
                auto arena_view = FastArenaView{arena};
                if (arena_view.can_allocate(size)) {
                    list_pop_head(&cutoff_list, FastArenaListOps{});
                    return AcquiredTypedResource<FastArena>::acquired(arena);
                }
                // if we failed, then we have to decide if it is worth keeping
                if (size > 2 * arena_view.remaining()) {
                    list_pop_head(&cutoff_list, FastArenaListOps{});
                    list_push_head(&recycled_list, &arena->list_entry, FastArenaListOps{});
                }
            }

            return AcquiredTypedResource<FastArena>::no_resource();
        }

        void release(FastArena* arena) {
            CUW3_CHECK(arena, "arena was null");
            CUW3_CHECK(FastArenaView{arena}.alignment() >= alignment, "arena has invalid alignment");
            CUW3_CHECK(FastArenaView{arena}.type() == (uint64)RegionChunkType::FastArenaSmallAllocator, "arena has invalid type");

            // anything better than null
            if (!current_arena) {
                current_arena = arena;
                return;
            }
            
            // update current_arena if arena is bigger
            {
                auto arena_view = FastArenaView{arena};
                auto current_arena_view = FastArenaView{current_arena};
                if (arena_view.remaining() > 2 * current_arena_view.remaining()) {
                    arena = std::exchange(current_arena, arena);
                }
            }

            // current_arena was updated (probably) now finally decide what to do with arena
            {
                auto arena_view = FastArenaView{arena};
                if (arena_view.remaining() >= size_cutoff) {
                    list_push_head(&free_list, &arena->list_entry, FastArenaListOps{});
                } else {
                    list_push_head(&cutoff_list, &arena->list_entry, FastArenaListOps{});
                }
            }
        }

        void extract(FastArena* arena) {
            CUW3_CHECK(arena, "arena was null");
            CUW3_CHECK(FastArenaView{arena}.alignment() >= alignment, "arena has invalid alignment");
            CUW3_CHECK(FastArenaView{arena}.type() == (uint64)RegionChunkType::FastArenaSmallAllocator, "arena has invalid type");

            if (arena == current_arena) {
                current_arena = nullptr;
            } else {
                list_erase(&arena->list_entry, FastArenaListOps{});
            }
        }

        FastArena* current_arena{};
        FastArenaListEntry free_list{};
        FastArenaListEntry cutoff_list{};
        FastArenaListEntry recycled_list{};
        uint64 alignment{};
        uint64 size_cutoff{};
    };

    struct FastArenaSmallBinsConfig {
        uint64 min_arena_alignment_log2{};
        uint64 max_arena_alignment_log2{};
        uint64 size_cutoff{};
    };

    struct FastArenaSmallBins {
        static constexpr uint64 align_axis = conf_max_fast_arenas;

        [[nodiscard]] static FastArenaSmallBins* create(Memory memory, const FastArenaSmallBinsConfig& config) {
            CUW3_CHECK_RETURN_VAL(memory.fits<FastArenaSmallBins>(), nullptr, "invalid memory");

            CUW3_CHECK_RETURN_VAL(config.max_arena_alignment_log2 >= config.min_arena_alignment_log2, nullptr, "max alignment must be greater or equal to minimal alignment");
            CUW3_CHECK_RETURN_VAL(config.max_arena_alignment_log2 <= conf_fast_arena_max_alignment_log2, nullptr, "max alignment is too big");
            CUW3_CHECK_RETURN_VAL(config.min_arena_alignment_log2 >= conf_fast_arena_min_alignment_log2, nullptr, "min alignment is too small");

            uint64 num_alignments = config.max_arena_alignment_log2 - config.min_arena_alignment_log2 + 1;
            CUW3_CHECK_RETURN_VAL(num_alignments <= align_axis, nullptr, "num of of alignments is too great");

            uint64 size_cutoff = align(config.size_cutoff, intpow2(config.max_arena_alignment_log2));

            auto* bins = new (memory.get()) FastArenaSmallBins{};
            bins->min_arena_alignment_log2 = config.min_arena_alignment_log2;
            bins->max_arena_alignment_log2 = config.max_arena_alignment_log2;
            bins->num_alignments = num_alignments;
            bins->size_cutoff = size_cutoff;
            for (uint i = 0; i < num_alignments; i++) {
                FastArenaSmallBinConfig bin_config{};
                bin_config.alignment = intpow2(config.min_arena_alignment_log2 + i);
                bin_config.size_cutoff = size_cutoff;

                auto* bin = FastArenaSmallBin::create(Memory::from(&bins->bins[i]), bin_config);
                CUW3_CHECK_RETURN_VAL(bin, nullptr, "failed to create bin");
            }
            return bins;
        }


        uint64 locate_alignment(uint64 alignment) const {
            uint64 alignment_log2 = intlog2(alignment);
            if (alignment_log2 > max_arena_alignment_log2) {
                return num_alignments;
            }
            alignment_log2 = std::max(alignment_log2, min_arena_alignment_log2);
            return alignment_log2 - min_arena_alignment_log2;
        }

        [[nodiscard]] AcquiredTypedResource<FastArena> acquire(uint64 size, uint64 alignment_id) {
            CUW3_CHECK(alignment_id < num_alignments, "invalid alignment");

            auto acquired = bins[alignment_id].acquire(size);
            if (acquired.status_acquired()) {
                CUW3_CHECK(total_arenas > 0, "invariant violation: positive amount of arenas expected.");
                total_arenas--;
            }
            return acquired;
        }

        void release(FastArena* arena, uint64 alignment_id) {
            CUW3_CHECK(alignment_id < num_alignments, "invalid alignment");

            bins[alignment_id].release(arena);
            total_arenas++;
        }

        void extract(FastArena* arena, uint64 alignment_id) {
            CUW3_CHECK(alignment_id < num_alignments, "invalid alignment");

            bins[alignment_id].extract(arena);
            CUW3_CHECK(total_arenas > 0, "invariant violation: positive amount of arenas expected.");
            total_arenas--;
        }

        // size_cutoff is max-aligned so no alignment required here
        bool can_allocate(uint64 size, uint64 alignment_id) const {
            return valid_alignment_id(alignment_id) && size <= size_cutoff;
        }

        bool valid_alignment_id(uint64 alignment_id) const {
            return alignment_id < num_alignments;
        }

        bool empty() const {
            return total_arenas == 0;
        }


        FastArenaSmallBin bins[align_axis] = {};
        uint64 min_arena_alignment_log2{};
        uint64 max_arena_alignment_log2{};
        uint64 num_alignments{};
        uint64 size_cutoff{};
        uint64 total_arenas{};
    };

    struct FastArenaSmallAllocatorConfig {
        FastArenaSmallBinsConfig bins_config{};
    };

    struct FastArenaSmallAllocator {
        [[nodiscard]] static FastArenaSmallAllocator* create(Memory memory, const FastArenaSmallAllocatorConfig& config) {
            CUW3_CHECK_RETURN_VAL(memory.fits<FastArenaSmallAllocator>(), nullptr, "invalid memory");

            auto* allocator = new (memory.get()) FastArenaSmallAllocator{};

            auto* bins = FastArenaSmallBins::create(Memory::from(&allocator->bins), config.bins_config);
            CUW3_CHECK_RETURN_VAL(bins, nullptr, "allocator: failed to create bins");

            // NOTE : we do not fill type label
            // allocator is retired by default (locked) 
            auto* retire_reclaim_entry = RetireReclaimEntryView::create(
                Memory::from(&allocator->retired_arenas.entry),
                (uint64)RetireReclaimFlags::RetiredFlag
            );
            CUW3_CHECK_RETURN_VAL(retire_reclaim_entry, nullptr, "failed to initialize retire-reclaim entry");

            return allocator;
        }


        uint64 get_size_cutoff() const {
            return bins.size_cutoff;
        }

        uint64 get_alignment(uint64 alignment_id) const {
            if (alignment_id >= bins.num_alignments) {
                return 0;
            }
            return intpow2(bins.min_arena_alignment_log2 + alignment_id);
        }

        uint64 get_num_alignments() const {
            return bins.num_alignments;
        }

        // arena will be extracted from the data structure
        [[nodiscard]] AcquiredTypedResource<FastArena> acquire(uint64 size, uint64 alignment) {
            auto alignment_id = bins.locate_alignment(alignment);
            if (!bins.can_allocate(size, alignment_id)) {
                return AcquiredTypedResource<FastArena>::failed();
            }
            if (!bins.valid_alignment_id(alignment_id)) {
                return AcquiredTypedResource<FastArena>::failed();
            }

            using enum AcquiredResource::Status;

            auto acquired = bins.acquire(size, alignment_id);
            switch (acquired.status) {
                case NoResource:
                case Acquired:
                    return acquired;
                case Failed:
                    CUW3_ABORT_CRITICAL("unreachable reached");
            }
            return AcquiredTypedResource<FastArena>::failed();
        }

        // arena is not in the data structure
        [[nodiscard]] void* allocate(AcquiredTypedResource<FastArena> arena, uint64 size) {
            CUW3_CHECK(arena.status_acquired(), "no arena");
            return allocate(arena.get(), size);
        }

        // arena is not in the data structure
        // we expect that we can allocate from the arena
        [[nodiscard]] void* allocate(FastArena* arena, uint64 size) {
            CUW3_CHECK(arena, "arena was null");
            CUW3_CHECK(size, "size was zero");

            auto arena_view = FastArenaView{arena};
            auto alignment = arena_view.alignment();
            auto alignment_id = bins.locate_alignment(alignment);

            CUW3_CHECK_RETURN_VAL_NO_MSG(bins.can_allocate(size, alignment_id), nullptr);
            CUW3_CHECK_RETURN_VAL_NO_MSG(bins.valid_alignment_id(alignment_id), nullptr);

            CUW3_CHECK(arena_view.alignment() >= alignment, "arena has invalid alignment");
            CUW3_CHECK(arena_view.type() == (uint64)RegionChunkType::FastArenaSmallAllocator, "arena has invalid type");

            void* allocated = arena_view.acquire(size);
            CUW3_CHECK(allocated, "invariant violation: we cannot allocate proper size");

            // we may release an arena that has less than size_cutoff of available size
            // this arena will go into the current arena or arena list then
            bins.release(arena, alignment_id);
            return allocated;
        }

        // arena is in the data structure
        [[nodiscard]] FastArena* deallocate(FastArena* arena, void* ptr, uint64 size) {
            CUW3_CHECK(arena, "arena was nullptr");
            CUW3_CHECK(size, "size was zero");

            auto arena_view = FastArenaView{arena};
            CUW3_CHECK(arena_view.type() == (uint64)RegionChunkType::FastArenaSmallAllocator, "arena has invalid type");

            arena_view.release(ptr, size);
            if (!arena_view.resettable()) {
                return nullptr;
            }

            // arena became resettable so we must to extract it, reset it and return 
            auto alignment_id = bins.locate_alignment(arena_view.alignment());
            bins.extract(arena, alignment_id);
            arena_view.reset();
            return arena;
        }

        // called from the non-owning thread
        // puts retired arenas in the list
        // arena pointer is stored as is - no offsetting will be required on reclaim
        [[nodiscard]] RetireReclaimPtr retire(FastArena* arena, void* ptr, uint64 size) {
            CUW3_CHECK(arena, "arena was null");
            CUW3_CHECK(ptr, "memory was null");
            CUW3_CHECK(size, "size was zero");

            auto arena_view = FastArenaView{arena};
            CUW3_CHECK(arena_view.type() == (uint64)RegionChunkType::FastArenaSmallAllocator, "arena does not belong to this allocator");

            auto retired = arena_view.retire_allocation(ptr, size);
            if (RetireReclaimFlagsHelper{retired}.retired()) {
                return retired;
            }

            auto retire_reclaim_entry_view = RetireReclaimPtrView{&retired_arenas.entry.head};
            return retire_reclaim_entry_view.retire_ptr(arena, FastArenaBackoff{}, FastArenaRetireReclaimResourceOps{});
        }

        // called by the owning thread
        // returns list of arenas
        [[nodiscard]] FastArenaReclaimList reclaim_arenas() {
            if (retired_arenas.entry.next_postponed) {
                return {(FastArena*)std::exchange(retired_arenas.entry.next_postponed, nullptr)};
            }
            // dementia comment: aborts if retired flag is not set, not the case here
            auto retired_arenas_view = RetireReclaimPtrView{&retired_arenas.entry.head};
            return {retired_arenas_view.reclaim().ptr<FastArena>()};
        }

        // called by the owning thread
        [[nodiscard]] FastArena* reclaim_arena(FastArena* arena) {
            auto arena_view = FastArenaView{arena};
            CUW3_CHECK(arena_view.type() == (uint64)RegionChunkType::FastArenaSmallAllocator, "arena does not belong to this allocator");

            auto alignment_id = bins.locate_alignment(arena_view.alignment());
            CUW3_CHECK(bins.valid_alignment_id(alignment_id), "invalid alignment");

            bins.extract(arena, alignment_id);

            arena_view.reclaim_allocations();
            if (arena_view.resettable()) {
                arena_view.reset();
                return arena;
            }

            bins.release(arena, alignment_id);
            return nullptr;
        }

        void postpone(FastArenaReclaimList list) {
            CUW3_CHECK(!retired_arenas.entry.next_postponed, "already postponed");

            retired_arenas.entry.next_postponed = list.head;
        }

        bool empty() const {
            return bins.empty();
        }


        FastArenaRetiredArenasRoot retired_arenas{};
        FastArenaSmallBins bins{};
    };
}