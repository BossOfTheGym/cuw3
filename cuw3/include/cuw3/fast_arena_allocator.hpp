#pragma once

#include "conf.hpp"
#include "list.hpp"
#include "bitmap.hpp"
#include "assert.hpp"
#include "backoff.hpp"
#include "retire_reclaim.hpp"
#include "region_chunk_handle.hpp"

#include <cmath>

namespace cuw3 {
    using FastArenaListEntry = DefaultListEntry;
    using FastArenaListOps = DefaultListOps<FastArenaListEntry>;

    using FastArenaBackoff = SimpleBackoff;

    // resource hierarchy
    // allocation -> arena -> (???)
    struct alignas(conf_cacheline) FastArena {
        static FastArena* list_entry_to_arena(FastArenaListEntry* list_entry) {
            return cuw3_field_to_obj(list_entry, FastArena, list_entry);
        }

        // cacheline 0
        RegionChunkHandleHeader region_chunk_header{};
        FastArenaListEntry list_entry{};

        uint64 freed{};
        uint64 top{};
        uint64 arena_memory_size{};
        uint64 arena_alignment{};

        alignas(8) void* arena_memory{};

        // cacheline 1
        RetireReclaimEntry retire_reclaim_entry{};
        uint64 pad1[4] = {};
    };

    static_assert(sizeof(FastArena) <= conf_control_block_size, "pack struct field better or increase size of the control block");


    struct FastArenaConfig {
        void* owner{};

        void* arena_handle{};
        void* arena_memory{};

        uint64 arena_handle_size{};
        uint64 arena_alignment{};
        uint64 arena_memory_size{};

        RetireReclaimRawPtr retire_reclaim_flags{};
    };

    // arena memory must be aligned to alignment
    // only aligned allocations can be made
    // remaining bytes must be always aligned
    struct FastArenaView {
        [[nodiscard]] static FastArena* create_fast_arena(const FastArenaConfig& config) {
            CUW3_CHECK_RETURN_VAL(config.owner, nullptr, "owner was null");
            CUW3_CHECK_RETURN_VAL(config.arena_handle, nullptr, "arena handle was null");
            CUW3_CHECK_RETURN_VAL(config.arena_memory, nullptr, "arena memory was null");
            CUW3_CHECK_RETURN_VAL(is_aligned(config.arena_handle, conf_cacheline), nullptr, "arena handle was not properly aligned");

            CUW3_CHECK_RETURN_VAL(config.arena_handle_size == conf_control_block_size, nullptr, "invalid size of arena handle");
            CUW3_CHECK_RETURN_VAL(is_alignment(config.arena_alignment) && config.arena_alignment >= conf_min_alloc_alignment, nullptr, "invalid alignment provided");
            CUW3_CHECK_RETURN_VAL(is_aligned(config.arena_memory_size, config.arena_alignment), nullptr, "arena size is not properly aligned");
            CUW3_CHECK_RETURN_VAL(is_aligned(config.arena_memory, config.arena_alignment), nullptr, "arena memory is not properly aligned");

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
            // TODO : check invariants instead of single check
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

        // TODO : rework release logic
        void release_unchecked(uint64 size) {
            release(arena->arena_memory, size);
        }

        // TODO : rework release logic
        void release(void* memory, uint64 size) {
            release_aligned(memory, align(size, arena->arena_alignment));
        }

        // TODO : rework release logic
        void release_aligned(void* memory, uint64 size) {
            // TODO : maybe switch to check-n-return?
            CUW3_ASSERT(is_aligned(size, alignment()), "size is not aligned");
            CUW3_ASSERT(has_memory_range(memory, size), "memory does not belong to the arena");

            uint64 new_freed = arena->freed + size;
            CUW3_CHECK(new_freed <= arena->arena_memory_size, "we have freed more than allocated");

            arena->freed = new_freed;
        }

        void reset() {
            arena->top = 0;
            arena->freed = 0;
        }

        bool has_memory_range(void* memory, uint64 size) {
            auto mem_val = (uintptr)memory;
            auto arena_start = (uintptr)arena->arena_memory;
            auto arena_stop = arena_start + arena->arena_memory_size;
            return arena_start <= mem_val && mem_val + size <= arena_stop;
        }

        bool resettable() const {
            return arena->freed == arena->top;
        }

        // empty means also resettable but little bit more special
        bool empty() const {
            return arena->freed == 0 && arena->top == 0;
        }

        bool full() const {
            return arena->top == arena->arena_memory_size;
        }

        bool in_list() const {
            return arena->list_entry.next != nullptr && arena->list_entry.prev != nullptr;
        }

        bool can_allocate_aligned(uint64 size_aligned) const {
            CUW3_ASSERT(is_aligned(size_aligned, arena->arena_alignment), "misaligned size");

            uint64 rem = remaining();
            CUW3_ASSERT(is_aligned(rem, arena->arena_alignment), "arena internal state misalignment");

            return rem >= size_aligned;
        }

        bool can_allocate(uint64 size) const {
            return can_allocate_aligned(align(size, arena->arena_alignment));
        }

        uint64 memory_size() const {
            return arena->arena_memory_size;
        }

        uint64 remaining() const {
            CUW3_ASSERT(arena->arena_memory_size >= arena->top, "top is greater than memory size");

            return arena->arena_memory_size - arena->top;
        }

        uint64 alignment() const {
            return arena->arena_alignment;
        }

        void* data_end() const {
            return advance_ptr(arena->arena_memory, arena->arena_memory_size);
        }

        FastArenaListEntry* list_entry() const {
            return &arena->list_entry;
        }

        void move_out_of_list() const {
            arena->list_entry.prev = nullptr;
            arena->list_entry.next = nullptr;
        }

        [[nodiscard]] RetireReclaimPtr retire_allocation(void* memory, uint64 size) {
            CUW3_ASSERT(has_memory_range(memory, size), "invalid memory range to retire");

            auto retire_reclaim_entry_view = RetireReclaimPtrView{&arena->retire_reclaim_entry.head};
            return retire_reclaim_entry_view.retire_data(size, FastArenaBackoff{});
        }

        void reclaim_allocations() {
            auto retire_reclaim_entry_view = RetireReclaimPtrView{&arena->retire_reclaim_entry.head};
            RetireReclaimPtr reclaimed = retire_reclaim_entry_view.reclaim();
            release_unchecked(reclaimed.value_shifted());
        }


        FastArena* arena{};
    };


    struct FastArenaBinsConfig {
        void* memory{};

        uint64 num_splits_log2{};

        uint64 min_arena_step_size_log2{};
        uint64 max_arena_step_size_log2{};

        uint64 min_arena_alignment_log2{};
        uint64 max_arena_alignment_log2{};
    };

    struct AcquiredResource {
        enum class Status {
            Failed,
            Acquired,
            NoResource,
        };

        static AcquiredResource acquired(void* resource) {
            return {Status::Acquired, resource};
        }

        static AcquiredResource no_resource() {
            return {Status::NoResource};
        }

        static AcquiredResource failed() {
            return {Status::Failed};
        }

        void* get() const {
            return resource;
        }

        bool status_failed() const {
            return status == Status::Failed;
        }

        bool status_acquired() const {
            return status == Status::Acquired;
        }

        bool status_no_resource() const {
            return status == Status::NoResource;
        }

        Status status{};
        void* resource{};
    };

    template<class T>
    struct AcquiredTypedResource : AcquiredResource {
        static AcquiredTypedResource acquired(void* resource) {
            return {AcquiredResource::acquired(resource)};
        }

        static AcquiredTypedResource no_resource() {
            return {AcquiredResource::no_resource()};
        }

        static AcquiredTypedResource failed() {
            return {AcquiredResource::failed()};
        }

        T* get() const {
            return (T*)AcquiredResource::get();
        }
    };

    // ALGORITHM DESCRIPTION
    // 
    // we have a range of steps that partition size range like that:
    // | s | s | 2s | 4s | 8s | 16s | ... | ks | all the other sizes
    //   ^
    //   |
    // 'zero' step
    //
    // where:
    //   s - min step size, s itself is power of two too
    //   ks - max step size, k is power of two
    // 
    // so ... min step size is pow of 2 as well as max step size
    //
    // let's say that STEPS is total number of steps that equals (max_step_pow2 - min_step_pow2 + 1) + 1
    //   +1 because step pow2 range includes right bound
    //   another +1 because we have another so called 'zero' step
    //
    // this way we have perfect binary structure: when we are at step number k (pow of 2)
    // then we have exactly 2^k of size space behind except the 'zero' step where we have exactly zero size space behind
    // this way we can identify the step for some size simply by applying intlog2
    // we also need this exceptional case with 'zero' step to correctly identify step base
    //
    // each step is split into some fixed (same for each step) amount of ... SPLITS!
    // which is also pow of two!
    // so the bigger the step the bigger the range split of sizes which makes sense as we require less granularity as memory request size grows
    //
    // total amount of step_splits is STEPS * SPLITS + 1
    // we reserve additional slot (bin) for all the sizes that go beyond the max step
    //
    // we calculate split number by substracting size from step_base and then dividing it by step_part = curr_step / splits
    //
    //
    // this algorithm can be used to identify both the proper bin for arena placement and search for the proper bin to acquire arena to allocate from
    // okay, let's look at this stuff again:
    // | s | s | 2s | 4s | 8s | 16s | ... | ks | all the other sizes
    //       ^    ^
    //       |    |
    //      sz   ar
    //
    // let's now notice that our algorithm is bullshit and does not work.
    // when we search a bin to acquire arena to allocate from we want to guarantee the success of the allocation.
    // that's why!
    // we need to increment by one the step-split value to obtain the bin with the proper arenas
    // 
    // ALSO! we have to round size up to the next split_part boundary (size search only) as
    // | split0 | split1 | ...
    //               ^
    // x--------- < these sizes are managed by the bin above (we do not include the beginning of the split range)
    // ^        ^
    // |        included!
    // |
    // not included!
    //
    // nice and handy property: step can equal to num_steps!
    // yeah, it would kind of create denormalized indexing ... but it would completely satisfy us
    //
    // alignment! we have predefined range of supported alignments!
    // it produces some difficulty because now we cannot define single min_alloc_size value for all entries
    // we have to calculate beforehand step_split index of the first available bin and min_alloc_size value for each entry
    // all bins with smaller index are not used except bin number zero that stores all recycled bins
    //
    struct FastArenaBins {
        struct FastArenaBin {
            FastArenaListEntry list_head{};
        };

        static constexpr uint64 align_axis = conf_max_fast_arenas;
        static constexpr uint64 step_axis = conf_max_fast_arena_lookup_steps;
        static constexpr uint64 split_axis = conf_max_fast_arena_lookup_split;
        static constexpr uint64 step_split_axis = (step_axis + 1) * split_axis + 1;

        using FastArenaBitmap = Bitmap<uint64, step_split_axis>;

        struct FastArenaStepSplitEntry {
            // arena selection array of lists helping to choose proper arena to allocate from
            FastArenaBin arenas[step_split_axis] = {};
            // bit mask of non-empty lists
            FastArenaBitmap present_arenas = {};
            // cached arena that we choose to allocate from without scanning arena array
            FastArena* cached_arena = {};
            // we update arena only if the incoming arena has twice as much capacity
            // or if we have refused to update it with arena with greater capacity for some predefined amount of times
            // not const
            uint64 cache_misses{};
            // min id of the bin that can be used by the algorithm
            // const
            uint64 min_step_split_id{};
            // min possible size of the allocation that can be made by any suitable arena
            // const
            uint64 min_alloc_size{};
        };


        [[nodiscard]] static FastArenaBins* create(const FastArenaBinsConfig& config) {
            CUW3_CHECK_RETURN_VAL(config.memory, nullptr, "memory was null");

            uint64 num_splits = intpow2(config.num_splits_log2);
            CUW3_CHECK_RETURN_VAL(num_splits <= split_axis, nullptr, "num_splits is too big");

            uint64 num_alignments = config.max_arena_alignment_log2 - config.min_arena_alignment_log2 + 1;
            CUW3_CHECK_RETURN_VAL(config.max_arena_alignment_log2 >= config.min_arena_alignment_log2, nullptr, "max_arena_alignment_log2 < min_arena_alignment_log2");
            CUW3_CHECK_RETURN_VAL(num_alignments <= align_axis, nullptr, "num_alignments is too big");

            uint64 num_steps = config.max_arena_step_size_log2 - config.min_arena_step_size_log2 + 2; // +1 for zero-step
            CUW3_CHECK_RETURN_VAL(config.max_arena_step_size_log2 >= config.min_arena_step_size_log2, nullptr, "max_arena_step_size_log2 < min_arena_step_size_log2");
            CUW3_CHECK_RETURN_VAL(num_steps <= step_axis, nullptr, "num_steps is too big");

            uint64 num_step_splits = num_splits * num_steps + 1;
            CUW3_CHECK_RETURN_VAL(config.min_arena_step_size_log2 >= config.num_splits_log2, nullptr, "step size must be bigger than number of splits");

            uint64 min_alloc_size = intpow2(config.min_arena_step_size_log2 - config.num_splits_log2);
            uint64 max_alloc_size = intpow2(config.max_arena_step_size_log2 + 1); // +1 because we cover the whole range of allocations! That's why!
            CUW3_CHECK_RETURN_VAL(max_alloc_size >= intpow2(config.max_arena_alignment_log2), nullptr, "constraints violation: max_alloc_size is too small compared to max alignment");

            auto* bins = new (config.memory) FastArenaBins{};
            bins->num_step_splits = num_step_splits;
            bins->num_splits = num_splits;
            bins->num_splits_log2 = config.num_splits_log2;

            bins->num_alignments = num_alignments;
            bins->min_arena_alignment_log2 = config.min_arena_alignment_log2;
            bins->max_arena_alignment_log2 = config.max_arena_alignment_log2;

            bins->num_steps = num_steps;
            bins->min_arena_step_size_log2 = config.min_arena_step_size_log2;
            bins->max_arena_step_size_log2 = config.max_arena_step_size_log2;

            bins->global_min_alloc_size = min_alloc_size;
            bins->global_max_alloc_size = max_alloc_size;

            for (uint alignment_id = 0; alignment_id < num_alignments; alignment_id++) {
                auto& entry = bins->step_split_entries[alignment_id];
                for (uint step_split_id = 0; step_split_id < num_step_splits; step_split_id++) {
                    auto& bin = entry.arenas[step_split_id];
                    list_init(&bin.list_head, FastArenaListOps{});
                }

                uint64 entry_min_alloc_size_hint = align(min_alloc_size, bins->get_alignment(alignment_id));
                auto info = bins->get_step_split_info(entry_min_alloc_size_hint, true);
                entry.min_alloc_size = info.get_step_split_size();
                entry.min_step_split_id = info.step_split_id;
            }

            return bins;
        }


        uint64 get_min_alloc_size(uint64 alignment_id) const {
            CUW3_ASSERT(alignment_id < num_alignments, "invalid alignment id");

            return step_split_entries[alignment_id].min_alloc_size;
        }

        uint64 get_alignment(uint64 alignment_id) const {
            CUW3_ASSERT(alignment_id < num_alignments, "invalid alignment id");

            return intpow2(min_arena_alignment_log2 + alignment_id);
        }

        uint64 get_global_min_alloc_size() const {
            return global_min_alloc_size;
        }

        uint64 get_global_max_alloc_size() const {
            return global_max_alloc_size;
        }

        uint64 get_global_maxmin_alloc_size() const {
            return step_split_entries[num_alignments - 1].min_alloc_size;
        }

        uint64 locate_alignment(uint64 alignment) const {
            uint64 alignment_log2 = intlog2(alignment);
            if (alignment_log2 > max_arena_alignment_log2) {
                return num_alignments;
            }
            alignment_log2 = std::max(alignment_log2, min_arena_alignment_log2);
            return alignment_log2 - min_arena_alignment_log2;
        }

        struct StepSplitInfo {
            uint64 get_step_size() const {
                return intpow2(step_size_log2);
            }

            uint64 get_step_split_size() const {
                return step_base + split_id * divpow2(get_step_size(), num_splits_log2);
            }

            uint64 step_id{};
            uint64 split_id{};
            uint64 step_split_id{}; // can become denormalized, that's okay
            uint64 step_base{};
            uint64 step_size_log2{};
            uint64 num_splits_log2{};
        };

        StepSplitInfo step_split_id_to_info(uint64 step_split_id) const {
            CUW3_CHECK(step_split_id < num_step_splits, "invalid step_split_id");

            uint64 step_id = divpow2(step_split_id, num_splits_log2);
            uint64 split_id = modpow2(step_split_id, num_splits_log2);

            uint64 step_base{};
            uint64 step_size_log2{};
            if (step_id > 0) {
                step_size_log2 = min_arena_step_size_log2 + step_id - 1;
                step_base = intpow2(step_size_log2);
            } else {
                step_size_log2 = min_arena_step_size_log2;
                step_base = 0;
            }
            return {step_id, split_id, step_split_id, step_base, step_size_log2, num_splits_log2};
        }

        StepSplitInfo get_step_split_info(uint64 size, bool align_split_up) const {
            uint64 step_size_log2 = std::min(intlog2(size), max_arena_step_size_log2);
            uint64 step_id = 0;
            uint64 step_base = 0;
            if (step_size_log2 < min_arena_step_size_log2) {
                step_size_log2 = min_arena_step_size_log2;
            } else {
                step_id = step_size_log2 - min_arena_step_size_log2 + 1; // +1 due to zero step
                step_base = intpow2(step_size_log2);
            }

            uint64 split_offset = (align_split_up ? intpow2(step_size_log2 - num_splits_log2) - 1 : 0);
            uint64 split_id = std::min(divpow2(mulpow2(size + split_offset - step_base, num_splits_log2), step_size_log2), num_splits);
            uint64 step_split_id = mulpow2(step_id, num_splits_log2) + split_id;
            CUW3_CHECK(step_split_id < num_step_splits, "invalid step_split index calculated");

            return {step_id, split_id, step_split_id, step_base, step_size_log2, num_splits_log2};
        }

        // size must be aligned beforehand if required
        uint64 locate_step_split_size(uint64 size) const {
            return get_step_split_info(size, true).step_split_id;
        }

        uint64 locate_step_split_arena(uint64 size) const {
            return get_step_split_info(size, false).step_split_id;
        }

        uint64 locate_step_split_arena_clamped(uint64 alignment_id, uint64 size) const {
            CUW3_CHECK(check_alignment_id(alignment_id), "invalid alignment");

            uint64 step_split_id = locate_step_split_arena(size);
            if (step_split_id < step_split_entries[alignment_id].min_step_split_id) {
                step_split_id = 0;
            }
            return step_split_id;
        }

        bool check_alignment(uint64 alignment) const {
            return is_pow2(alignment) && intpow2(min_arena_alignment_log2) <= alignment && alignment <= intpow2(max_arena_alignment_log2);
        }

        bool check_alignment_id(uint64 alignment_id) const {
            return alignment_id < num_alignments;
        } 

        bool can_allocate(uint64 size, uint64 alignment) {
            if (!check_alignment(alignment)) {
                return false;
            }
            return can_allocate_aligned(align(size, alignment), locate_alignment(alignment));
        }

        bool can_allocate_aligned(uint64 size_aligned, uint64 alignment_id) const {
            CUW3_ASSERT(check_alignment_id(alignment_id), "invalid alignment id");
            CUW3_ASSERT(is_aligned(size_aligned, get_alignment(alignment_id)), "aligned size expected");

            auto& entry = step_split_entries[alignment_id];
            return entry.min_alloc_size <= size_aligned && size_aligned <= global_max_alloc_size;
        }


        // can_allocate returned true
        [[nodiscard]] AcquiredTypedResource<FastArena> _acquire_cached_arena(uint64 size_aligned, uint64 alignment_id) {
            CUW3_ASSERT(check_alignment_id(alignment_id), "invalid alignment id");
            CUW3_ASSERT(is_aligned(size_aligned, get_alignment(alignment_id)), "aligned size expected");
            
            auto& entry = step_split_entries[alignment_id];
            auto arena_view = FastArenaView{entry.cached_arena};
            CUW3_CHECK(arena_view.remaining() >= entry.min_alloc_size, "invariant violation: cached arena has less space that min_alloc_size");

            if (arena_view.can_allocate_aligned(size_aligned)) {
                return AcquiredTypedResource<FastArena>::acquired(std::exchange(entry.cached_arena, nullptr));
            }
            return AcquiredTypedResource<FastArena>::no_resource();
        }

        // _can_allocate => true
        [[nodiscard]] AcquiredTypedResource<FastArena> _acquire_bin_arena(uint64 size_aligned, uint64 alignment_id) {
            CUW3_ASSERT(check_alignment_id(alignment_id), "invalid alignment id");
            CUW3_ASSERT(is_aligned(size_aligned, get_alignment(alignment_id)), "aligned size expected");

            auto& entry = step_split_entries[alignment_id];
            auto step_split_id = locate_step_split_size(size_aligned);
            CUW3_ASSERT(step_split_id >= entry.min_step_split_id, "attempt to acquire arena from improper bin");

            auto present_arena_bin_id = entry.present_arenas.get_first_set(step_split_id);
            if (present_arena_bin_id == entry.present_arenas.null_bit) {
                return AcquiredTypedResource<FastArena>::no_resource();
            }

            auto& bin = entry.arenas[present_arena_bin_id];
            CUW3_CHECK(!list_empty(&bin.list_head, FastArenaListOps{}), "invariant violation: bit set but list is empty");

            auto* arena = FastArena::list_entry_to_arena(list_pop_head(&bin.list_head, FastArenaListOps{}));
            auto arena_view = FastArenaView{arena};
            arena_view.move_out_of_list();
            if (list_empty(&bin.list_head, FastArenaListOps{})) {
                entry.present_arenas.unset(present_arena_bin_id);
            }
            CUW3_CHECK(arena_view.can_allocate_aligned(size_aligned), "invariant violation: arena was placed in the improper bin");

            return AcquiredTypedResource<FastArena>::acquired(arena);
        }

        // returns pointer to the arena that is no longer used
        [[nodiscard]] FastArena* _try_update_cached_arena(FastArena* arena, uint64 alignment_id) {
            CUW3_ASSERT(arena, "arena was null");
            CUW3_ASSERT(check_alignment_id(alignment_id), "invalid alignment id");

            // decision to use offered arena as a cached one
            auto arena_view = FastArenaView{arena};
            auto& entry = step_split_entries[alignment_id];
            if (!entry.cached_arena) {
                if (FastArenaView{arena}.remaining() >= entry.min_alloc_size) {
                    entry.cached_arena = arena;
                    return nullptr;
                }
                return arena;
            }

            // decision to update existing arena
            uint64 curr_remaining = FastArenaView{entry.cached_arena}.remaining();
            uint64 new_remaining = arena_view.remaining();
            if (new_remaining >= 2 * curr_remaining) {
                entry.cache_misses = 0;
                return std::exchange(entry.cached_arena, arena);
            } else if (new_remaining > curr_remaining) {
                entry.cache_misses++;
                if (entry.cache_misses == 4) {
                    entry.cache_misses = 0;
                    return std::exchange(entry.cached_arena, arena);
                }
            }
            return arena;
        }

        // arena is not in any list
        void _put_into_bins(FastArena* arena, uint64 alignment_id) {
            CUW3_ASSERT(arena, "arena was null");
            CUW3_ASSERT(!FastArenaView{arena}.in_list(), "arena must not be in any list");
            CUW3_ASSERT(check_alignment_id(alignment_id), "invalid alignment");

            auto arena_view = FastArenaView{arena};
            auto& entry = step_split_entries[alignment_id];

            auto step_split = locate_step_split_arena(arena_view.remaining());
            if (step_split < entry.min_step_split_id) {
                step_split = 0; // recycled arenas are stored here
            }
            auto& bin = entry.arenas[step_split];
            list_push_head(&bin.list_head, arena_view.list_entry(), FastArenaListOps{});
            entry.present_arenas.set(step_split);
        }

        void _release_arena(FastArena* arena, uint64 alignment_id) {
            arena = _try_update_cached_arena(arena, alignment_id);
            if (!arena) {
                return;
            }
            _put_into_bins(arena, alignment_id);
        }

        [[nodiscard]] AcquiredTypedResource<FastArena> _acquire_arena(uint64 size_aligned, uint64 alignment_id) {
            CUW3_ASSERT(is_aligned(size_aligned, get_alignment(alignment_id)), "invalid size");
            CUW3_ASSERT(check_alignment_id(alignment_id), "invalid alignment");
            CUW3_ASSERT(can_allocate_aligned(size_aligned, alignment_id), "cannot allocate");

            using enum AcquiredTypedResource<FastArena>::Status;

            auto acquired = _acquire_cached_arena(size_aligned, alignment_id);
            switch (acquired.status) {
                case Failed:
                case Acquired:
                    return acquired;
                case NoResource:
                    break;
                default:
                    CUW3_ABORT("unreachable reached");
            }
            
            acquired = _acquire_bin_arena(size_aligned, alignment_id);
            switch (acquired.status) {
                case Failed:
                case Acquired:
                    return acquired;
                case NoResource:
                    break;
                default:
                    CUW3_ABORT("unreachable reached");
            }

            return AcquiredTypedResource<FastArena>::no_resource();
        }


        // for testing purposes only
        bool _has_any_available_arenas(uint64 alignment_id) const {
            CUW3_CHECK(check_alignment_id(alignment_id), "invalid alignment id");

            auto& entry = step_split_entries[alignment_id];
            return entry.present_arenas.any_set(entry.min_step_split_id) || entry.cached_arena;
        }

        // for testing purposes only
        [[nodiscard]] uint64 _sample_allocation_upper_bound(uint64 alignment_id, uint64 seed) const {
            CUW3_CHECK(check_alignment_id(alignment_id), "invalid alignment id");
            
            auto& entry = step_split_entries[alignment_id];
            uint64 step_split_id = entry.present_arenas.sample_set_bit(seed, entry.min_step_split_id);
            if (step_split_id != entry.present_arenas.null_bit) {
                auto info = step_split_id_to_info(step_split_id);
                return info.get_step_split_size();
            }
            if (entry.cached_arena) {
                return FastArenaView{entry.cached_arena}.remaining();
            }
            return 0;
        }

        // for testing purposes only
        bool _is_allocator_empty() const {
            for (uint alignment_id = 0; alignment_id < num_alignments; alignment_id++) {
                auto& entry = step_split_entries[alignment_id];
                if (entry.cached_arena) {
                    return false;
                }
                if (!entry.present_arenas.all_reset()) {
                    return false;
                }
                for (uint bin = 0; bin < num_step_splits; bin++) {
                    if (!list_empty(&entry.arenas[bin], FastArenaListOps{})) {
                        return false;
                    }
                }
            }
            return true;
        }


        // get arena to allocate from
        // arena is removed from the data structure
        // may return null
        [[nodiscard]] AcquiredTypedResource<FastArena> acquire_arena(uint64 size, uint64 alignment) {
            auto alignment_id = locate_alignment(alignment);
            if (alignment_id == num_alignments) {
                return AcquiredTypedResource<FastArena>::failed();
            }

            uint64 size_aligned = align(size, alignment);
            if (!can_allocate_aligned(size_aligned, alignment_id)) {
                return AcquiredTypedResource<FastArena>::failed();
            }

            return _acquire_arena(size_aligned, alignment_id);
        }

        // return arena back into the data structure
        // arena can get into the recycling bin if necessary (if there is not much space left)
        void release_arena(FastArena* arena) {
            CUW3_CHECK(arena, "arena was null");

            auto arena_view = FastArenaView{arena};
            CUW3_CHECK(!arena_view.in_list(), "arena must be out of any list");
            CUW3_CHECK(!arena_view.resettable(), "arena was empty, must have been recycled");
            
            auto alignment_id = locate_alignment(arena_view.alignment());
            CUW3_CHECK(check_alignment_id(alignment_id), "invalid alignment");
            CUW3_CHECK(arena_view.memory_size() >= step_split_entries[alignment_id].min_alloc_size, "arena is too fucking small");

            _release_arena(arena, alignment_id);
        }

        // arena is guaranteed to be in the data structure
        // we are about to modify it 
        void extract_arena(FastArena* arena) {
            CUW3_CHECK(arena, "arena was null");

            auto arena_view = FastArenaView{arena};

            auto alignment_id = locate_alignment(arena_view.alignment());
            CUW3_CHECK(check_alignment_id(alignment_id), "invalid alignment");

            auto& entry = step_split_entries[alignment_id];
            if (arena == entry.cached_arena) {
                entry.cached_arena = nullptr;
                return;
            }

            // TODO : this shit repeats itself
            auto step_split_id = locate_step_split_arena_clamped(alignment_id, arena_view.remaining());
            list_erase(arena_view.list_entry(), FastArenaListOps{});
            arena_view.move_out_of_list();
            if (list_empty(&entry.arenas[step_split_id].list_head, FastArenaListOps{})) {
                entry.present_arenas.unset(step_split_id);
            }
        }

        
        // TODO : move allocate, deallocate to the allocator
        // arena was either previously acquired or has just been created
        // either way arena is not in the data structure
        [[nodiscard]] void* allocate(AcquiredTypedResource<FastArena> arena, uint64 size) {
            CUW3_CHECK(arena.status_acquired(), "arena was null");
            return allocate(arena.get(), size);
        }

        // same as method above but used for fresh arenas
        [[nodiscard]] void* allocate(FastArena* arena, uint64 size) {
            CUW3_CHECK(arena, "arena was null");
            CUW3_CHECK(size, "cannot make zero allocation");
            
            auto arena_view = FastArenaView{arena};
            CUW3_CHECK(!arena_view.in_list(), "arena must not be in any list");
            CUW3_CHECK(arena_view.empty() || !arena_view.resettable(), "arena must be either fresh (empty) or not resettable (we must have resetted it before)");

            void* allocated = arena_view.acquire(size);
            CUW3_CHECK(allocated, "arena must have had enough space");
            
            release_arena(arena);
            return allocated;
        }

        // arena is retrieved externally (in some magic way)
        // arena is in the data structure (either cached or in the bins)
        // returns arena if it has become empty
        [[nodiscard]] FastArena* deallocate(FastArena* arena, void* memory, uint64 size) {
            CUW3_CHECK(arena, "arena was null");
            CUW3_CHECK(size, "size was zero");

            auto arena_view = FastArenaView{arena};
            arena_view.release(memory, size);
            if (!arena_view.resettable()) {
                return nullptr;
            }

            auto alignment_id = locate_alignment(arena_view.alignment());
            CUW3_CHECK(check_alignment_id(alignment_id), "invalid arena alignment");

            auto& entry = step_split_entries[alignment_id];
            if (arena == entry.cached_arena) {
                CUW3_CHECK(!arena_view.in_list(), "arena is cached so it must not be in any list");

                entry.cached_arena = nullptr;
                arena_view.reset();
                return arena;
            }

            CUW3_CHECK(arena_view.in_list(), "arena must have been present in some bin");

            auto step_split_id = locate_step_split_arena_clamped(alignment_id, arena_view.remaining());
            list_erase(arena_view.list_entry(), FastArenaListOps{});
            arena_view.move_out_of_list();
            if (list_empty(&entry.arenas[step_split_id].list_head, FastArenaListOps{})) {
                entry.present_arenas.unset(step_split_id);
            }
            arena_view.reset();
            return arena;
        }


        FastArenaStepSplitEntry step_split_entries[align_axis] = {};

        uint64 num_step_splits{}; 
        uint64 num_splits{};
        uint64 num_splits_log2{};
        
        uint64 num_steps{};
        uint64 min_arena_step_size_log2{};
        uint64 max_arena_step_size_log2{};
        
        uint64 num_alignments{};
        uint64 min_arena_alignment_log2{};
        uint64 max_arena_alignment_log2{};

        uint64 global_min_alloc_size{};
        uint64 global_max_alloc_size{};
    };

    struct FastArenaAllocatorConfig {
        void* memory{};

        FastArenaBinsConfig bins_config{};
    };

    struct FastArenaReclaimList {
        FastArena* peek() {
            return head;
        }

        [[nodiscard]] FastArena* pop() {
            CUW3_ASSERT(head, "attempt to pop from empty list");

            auto* arena = head;
            head = (FastArena*)std::exchange(head->retire_reclaim_entry.next, nullptr);
            return arena;
        }

        bool empty() const {
            return head;
        }

        FastArena* head{};
    };

    // does not round up size to the min possible allocatable size
    // responsibility to locate owning arena is external to this data structure
    struct FastArenaAllocator {
        struct alignas(conf_cacheline) RetiredArenasRoot {
            RetireReclaimEntry entry{};
        };

        struct FastArenaRetireReclaimResourceOps {
            void set_next(void* arena, void* retired_arena_list) {
                ((FastArena*)arena)->retire_reclaim_entry.next = retired_arena_list;
            }
        };


        [[nodiscard]] static FastArenaAllocator* create(const FastArenaAllocatorConfig& config) {
            CUW3_CHECK_RETURN_VAL(config.memory, nullptr, "memory was null");

            auto* allocator = new (config.memory) FastArenaAllocator{};
            auto* bins = FastArenaBins::create(config.bins_config);
            if (!bins) {
                return nullptr;
            }
            (void)RetireReclaimEntryView::create(&allocator->retired_arenas.entry, (RetireReclaimRawPtr)RetireReclaimFlags::RetiredFlag);
            return allocator;
        }


        // for testing purposes only
        [[nodiscard]] uint64 _sample_allocation_upper_bound(uint64 alignment_id, uint64 seed) const {
            uint64 upper_bound = fast_arena_bins._sample_allocation_upper_bound(alignment_id, seed);
            if (upper_bound == 0) {
                CUW3_CHECK(fast_arena_bins._has_any_available_arenas(alignment_id), "sample function does not work properly");
            }
            return upper_bound;
        }

        // for testing purposes only
        bool _is_allocator_empty() const {
            return fast_arena_bins._is_allocator_empty();
        }


        bool supports_alignment(uint64 alignment) const {
            return fast_arena_bins.check_alignment(alignment);
        }

        uint64 get_num_alignments() const {
            return fast_arena_bins.num_alignments;
        }

        uint64 get_min_alignment() const {
            return get_alignment(0);
        }

        uint64 get_max_alignment() const {
            return get_alignment(get_num_alignments() - 1);
        }

        uint64 get_alignment(uint64 alignment_id) const {
            return fast_arena_bins.get_alignment(alignment_id);
        }

        uint64 get_min_alloc_size(uint64 alignment_id) const {
            if (alignment_id < get_num_alignments()) {
                return fast_arena_bins.get_min_alloc_size(alignment_id);
            }
            return 0;
        }

        uint64 get_max_alloc_size() const {
            return fast_arena_bins.get_global_max_alloc_size();
        }

        uint64 get_maxmin_alloc_size() const {
            return fast_arena_bins.get_global_maxmin_alloc_size();
        }


        // acquire arena for allocation, may be null
        [[nodiscard]] AcquiredTypedResource<FastArena> acquire_arena(uint64 size, uint64 alignment) {
            return fast_arena_bins.acquire_arena(size, alignment);
        }

        // returns allocated memory, expected to never return null
        [[nodiscard]] void* allocate(AcquiredTypedResource<FastArena> arena, uint64 size) {
            return fast_arena_bins.allocate(arena, size);
        }

        // returns allocated memory, expected to never be null
        [[nodiscard]] void* allocate(FastArena* arena, uint64 size) {
            return fast_arena_bins.allocate(arena, size);
        }

        // returns empty arena or null
        [[nodiscard]] FastArena* deallocate(FastArena* arena, void* memory, uint64 size) {
            return fast_arena_bins.deallocate(arena, memory, size);
        }

        // called from the non-owning thread
        // puts retired arenas in the list
        // arena pointer is stored as is - no offsetting will be required on reclaim
        [[nodiscard]] RetireReclaimPtr retire(FastArena* arena, void* memory, uint64 size) {
            CUW3_ASSERT(arena, "arena was null");
            CUW3_ASSERT(size, "size was zero");

            auto arena_view = FastArenaView{arena};
            auto old_resource = arena_view.retire_allocation(memory, size);
            if (RetireReclaimFlagsHelper{old_resource}.retired()) {
                return old_resource; // we observed the resource as retired, we cannot proceed
            }

            auto retired_arenas_view = RetireReclaimPtrView{&retired_arenas.entry.head};
            return retired_arenas_view.retire_ptr(arena, FastArenaBackoff{}, FastArenaRetireReclaimResourceOps{});
        }

        // called by the owning thread
        // returns list of arenas
        [[nodiscard]] FastArenaReclaimList reclaim() {
            if (retired_arenas.entry.next_postponed) {
                return {(FastArena*)std::exchange(retired_arenas.entry.next_postponed, nullptr)};
            }
            auto retired_arenas_view = RetireReclaimPtrView{&retired_arenas.entry.head};
            return {retired_arenas_view.reclaim().ptr<FastArena>()};
        }

        void postpone(FastArenaReclaimList list) {
            CUW3_CHECK(!retired_arenas.entry.next_postponed, "already postponed");

            retired_arenas.entry.next_postponed = list.head;
        }


        RetiredArenasRoot retired_arenas{};
        FastArenaBins fast_arena_bins{};
    };
}