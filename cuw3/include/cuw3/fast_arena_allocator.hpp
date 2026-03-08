#pragma once

#include "conf.hpp"
#include "list.hpp"
#include "utils.hpp"
#include "bitmap.hpp"
#include "assert.hpp"
#include "backoff.hpp"
#include "retire_reclaim.hpp"
#include "region_chunk_handle.hpp"

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

        void* arena_memory{};
        uint64 arena_memory_size{};
        uint64 arena_alignment{};

        RetireReclaimRawPtr retire_reclaim_flags{};
    };

    // arena memory must be aligned to alignment
    // only aligned allocations can be made
    // remaining bytes must be always aligned
    struct FastArenaView {
        [[nodiscard]] static FastArena* create(Memory memory, const FastArenaConfig& config) {
            CUW3_CHECK_RETURN_VAL(memory.fits<FastArena>(conf_control_block_size, conf_cacheline), nullptr, "inappropriate memory");

            CUW3_CHECK_RETURN_VAL(config.owner, nullptr, "owner was null");
            CUW3_CHECK_RETURN_VAL(config.arena_memory, nullptr, "arena memory was null");

            CUW3_CHECK_RETURN_VAL(is_alignment(config.arena_alignment) && config.arena_alignment >= conf_min_alloc_alignment, nullptr, "invalid alignment provided");
            CUW3_CHECK_RETURN_VAL(is_aligned(config.arena_memory_size, config.arena_alignment), nullptr, "arena size is not properly aligned");
            CUW3_CHECK_RETURN_VAL(is_aligned(config.arena_memory, config.arena_alignment), nullptr, "arena memory is not properly aligned");

            auto* arena = new (memory.get()) FastArena{};
            RegionChunkHandleHeaderView{&arena->region_chunk_header}.start_chunk_lifetime(config.owner, (uint64)RegionChunkType::FastArena);

            arena->arena_alignment = config.arena_alignment;
            arena->top = 0;
            arena->arena_memory_size = config.arena_memory_size;
            arena->arena_memory = config.arena_memory;

            auto* retire_reclaim_entry = RetireReclaimEntryView::create(
                Memory::from(&arena->retire_reclaim_entry), config.retire_reclaim_flags, (uint32)RegionChunkType::FastArena, offsetof(FastArena, retire_reclaim_entry)
            );
            CUW3_CHECK_RETURN_VAL(retire_reclaim_entry, nullptr, "fast_arena: failed to create retire_reclaim_entry");

            return arena;
        }

        [[nodiscard]] static FastArenaView create_view(Memory memory, const FastArenaConfig& config) {
            return {create(memory, config)};
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

        void release_reclaimed(uint64 size) {
            release(arena->arena_memory, size); // dummy ptr
        }

        void release(void* memory, uint64 size) {
            release_aligned(memory, align(size, arena->arena_alignment));
        }

        void release_aligned(void* memory, uint64 size) {
            CUW3_ASSERT(is_aligned(size, alignment()), "size is not aligned");
            CUW3_ASSERT(has_memory_range(memory, size), "memory does not belong to the arena");

            uint64 new_freed = arena->freed + size;
            CUW3_CHECK(new_freed <= arena->top, "we have freed more than allocated");

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
            CUW3_CHECK(has_memory_range(memory, size), "invalid memory range to retire");

            auto retire_reclaim_entry_view = RetireReclaimPtrView{&arena->retire_reclaim_entry.head};
            return retire_reclaim_entry_view.retire_data(size, FastArenaBackoff{});
        }

        // fast arena is a leaf resource so we always reclaim and reset    
        void reclaim_allocations() {
            auto retire_reclaim_entry_view = RetireReclaimPtrView{&arena->retire_reclaim_entry.head};
            RetireReclaimPtr reclaimed = retire_reclaim_entry_view.reclaim_reset();
            release_reclaimed(reclaimed.value_shifted());
        }


        FastArena* arena{};
    };


    struct FastArenaBinsConfig {
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
        static constexpr uint64 align_axis = conf_max_fast_arenas;
        static constexpr uint64 step_axis = conf_max_fast_arena_lookup_steps;
        static constexpr uint64 split_axis = conf_max_fast_arena_lookup_split;
        static constexpr uint64 step_split_axis = (step_axis + 1) * split_axis + 1;


        using FastArenaBitmap = Bitmap<uint64, step_split_axis>;

        struct FastArenaBin {
            [[nodiscard]] FastArena* pop() {
                CUW3_ASSERT(!empty(), "list was empty");

                auto popped = FastArena::list_entry_to_arena(list_pop_head(&list_head, FastArenaListOps{}));
                FastArenaView{popped}.move_out_of_list();
                return popped;
            }

            void push(FastArena* arena) {
                CUW3_ASSERT(arena, "arena was null");

                list_push_head(&list_head, &arena->list_entry, FastArenaListOps{});
            }

            void extract(FastArena* arena) {
                CUW3_ASSERT(arena, "arena was null");

                auto arena_view = FastArenaView{arena};
                CUW3_CHECK(arena_view.in_list(), "arena was not in any list");
                CUW3_CHECK(!empty(), "attempt to extract from an empty list");

                list_erase(arena_view.list_entry(), FastArenaListOps{});
                arena_view.move_out_of_list();
            }

            bool empty() const {
                return list_empty(&list_head, FastArenaListOps{});
            }

            FastArenaListEntry list_head{};
        };

        struct FastArenaStepSplitInfo {
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

        struct FastArenaBinsInfo {
            [[nodiscard]] static FastArenaBinsInfo* create(Memory memory, const FastArenaBinsConfig& config) {
                CUW3_CHECK_RETURN_VAL(memory.fits<FastArenaBinsInfo>(), nullptr, "invalid memory");

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

                auto* bins_info = new (memory.get()) FastArenaBinsInfo{};
                bins_info->num_step_splits = num_step_splits;
                bins_info->num_splits = num_splits;
                bins_info->num_splits_log2 = config.num_splits_log2;

                bins_info->num_alignments = num_alignments;
                bins_info->min_arena_alignment_log2 = config.min_arena_alignment_log2;
                bins_info->max_arena_alignment_log2 = config.max_arena_alignment_log2;

                bins_info->num_steps = num_steps;
                bins_info->min_arena_step_size_log2 = config.min_arena_step_size_log2;
                bins_info->max_arena_step_size_log2 = config.max_arena_step_size_log2;

                bins_info->global_min_alloc_size = min_alloc_size;
                bins_info->global_max_alloc_size = max_alloc_size;

                for (uint64 alignment_id = 0; alignment_id < bins_info->num_alignments; alignment_id++) {
                    uint64  alignment = bins_info->get_alignment(alignment_id);
                    uint64 min_alloc_size_hint = align(bins_info->global_min_alloc_size, alignment);
                    auto info = bins_info->get_step_split_info(min_alloc_size_hint, true);

                    bins_info->min_alloc_size[alignment_id] = info.get_step_split_size();
                    bins_info->min_step_split_id[alignment_id] = info.step_split_id;
                }

                return bins_info;
            }


            FastArenaStepSplitInfo step_split_id_to_info(uint64 step_split_id) const {
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

            FastArenaStepSplitInfo get_step_split_info(uint64 size, bool align_split_up) const {
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


            uint64 get_num_step_splits() const {
                return num_step_splits;
            }

            uint64 get_min_alloc_size(uint64 alignment_id) const {
                CUW3_ASSERT(alignment_id < num_alignments, "invalid alignment id");

                return min_alloc_size[alignment_id];
            }

            uint64 get_min_step_split_id(uint64 alignment_id) const {
                CUW3_ASSERT(alignment_id < num_alignments, "invalid alignment id");

                return min_step_split_id[alignment_id];
            }

            uint64 get_num_alignments() const {
                return num_alignments;
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
                return min_alloc_size[num_alignments - 1];
            }


            uint64 locate_alignment(uint64 alignment) const {
                uint64 alignment_log2 = intlog2(alignment);
                if (alignment_log2 > max_arena_alignment_log2) {
                    return num_alignments;
                }
                alignment_log2 = std::max(alignment_log2, min_arena_alignment_log2);
                return alignment_log2 - min_arena_alignment_log2;
            }

            // size must be aligned beforehand if required
            uint64 locate_step_split_size(uint64 size) const {
                return get_step_split_info(size, true).step_split_id;
            }

            uint64 locate_step_split_arena(uint64 size) const {
                return get_step_split_info(size, false).step_split_id;
            }

            uint64 locate_step_split_size_clamped(uint64 alignment_id, uint64 size) const {
                CUW3_CHECK(check_alignment_id(alignment_id), "invalid alignment");

                uint64 step_split_id = locate_step_split_size(size);
                if (step_split_id < min_step_split_id[alignment_id]) {
                    step_split_id = min_step_split_id[alignment_id];
                }
                return step_split_id;
            }

            uint64 locate_step_split_arena_clamped(uint64 alignment_id, uint64 size) const {
                CUW3_CHECK(check_alignment_id(alignment_id), "invalid alignment");

                uint64 step_split_id = locate_step_split_arena(size);
                if (step_split_id < min_step_split_id[alignment_id]) {
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

                return size_aligned <= global_max_alloc_size;
            }


            uint64 min_alloc_size[align_axis]{};
            uint64 min_step_split_id[align_axis]{};

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

        struct FastArenaStepSplitEntry {
            static constexpr auto null_step_split_id = FastArenaBitmap::null_bit;

            [[nodiscard]] static FastArenaStepSplitEntry* create(
                Memory memory,
                uint64 alignment,
                uint64 num_step_split_bins,
                uint64 min_step_split_id,
                uint64 min_alloc_size
            ) {
                CUW3_CHECK_RETURN_VAL(memory.fits<FastArenaStepSplitEntry>(), nullptr, "invalid memory");
                CUW3_CHECK_RETURN_VAL(num_step_split_bins < step_split_axis, nullptr, "to big number of step-split bins");
                CUW3_CHECK_RETURN_VAL(min_step_split_id > 0 && min_step_split_id < step_split_axis, nullptr, "too big value for min_step_split_id or value equals to zero");

                auto* entry = new (memory.get()) FastArenaStepSplitEntry{};
                for (uint i = 0; i < num_step_split_bins; i++) {
                    list_init(&entry->arenas[i].list_head, FastArenaListOps{});
                }
                entry->alignment = alignment;
                entry->num_step_split_bins = num_step_split_bins;
                entry->min_step_split_id = min_step_split_id;
                entry->min_alloc_size = min_alloc_size;
                return entry;
            }


            [[nodiscard]] AcquiredTypedResource<FastArena> _acquire_cached_arena(uint64 size_aligned) {
                CUW3_ASSERT(is_aligned(size_aligned, alignment), "aligned size expected");
                
                if (!cached_arena) {
                    return AcquiredTypedResource<FastArena>::no_resource();
                }
                auto arena_view = FastArenaView{cached_arena};
                if (arena_view.can_allocate_aligned(size_aligned)) {
                    return AcquiredTypedResource<FastArena>::acquired(std::exchange(cached_arena, nullptr));
                }
                return AcquiredTypedResource<FastArena>::no_resource();
            }

            [[nodiscard]] AcquiredTypedResource<FastArena> _acquire_bin_arena(uint64 step_split_id, uint64 size_aligned) {
                CUW3_ASSERT(is_aligned(size_aligned, alignment), "aligned size expected");
                CUW3_ASSERT(min_step_split_id <= step_split_id && step_split_id < num_step_split_bins, "attempt to acquire arena from the improper bin");

                auto present_arena_bin_id = present_arenas.get_first_set(step_split_id);
                if (present_arena_bin_id == present_arenas.null_bit) {
                    return AcquiredTypedResource<FastArena>::no_resource();
                }

                auto& bin = arenas[present_arena_bin_id];
                CUW3_CHECK(bin.empty(), "invariant violation: bit set but list is empty");

                auto* arena = bin.pop();
                if (bin.empty()) {
                    present_arenas.unset(present_arena_bin_id);
                }

                auto arena_view = FastArenaView{arena};
                CUW3_CHECK(arena_view.remaining() >= min_alloc_size, "invariant violation: cached arena has less space that min_alloc_size");

                return AcquiredTypedResource<FastArena>::acquired(arena);
            }

            [[nodiscard]] FastArena* _try_update_cached_arena(FastArena* arena) {
                CUW3_ASSERT(arena, "arena was null");

                // decision to use offered arena as a cached one
                auto arena_view = FastArenaView{arena};
                if (!cached_arena) {
                    cached_arena = arena;
                    return nullptr;
                }

                // decision to update existing arena
                auto cached_arena_view = FastArenaView{cached_arena};
                uint64 curr_remaining = cached_arena_view.remaining();
                uint64 new_remaining = arena_view.remaining();
                if (new_remaining >= 2 * curr_remaining) {
                    return std::exchange(cached_arena, arena);
                }
                return arena;
            }

            // arena is not in any list
            void _put_arena(FastArena* arena, uint64 step_split_id) {
                CUW3_ASSERT(arena, "arena was null");
                CUW3_ASSERT(step_split_id == 0 || min_step_split_id <= step_split_id && step_split_id < num_step_split_bins, "invalid step_split value");

                auto arena_view = FastArenaView{arena};
                CUW3_CHECK(!arena_view.in_list(), "arena must not be in any list");

                auto& bin = arenas[step_split_id];
                auto was_empty = bin.empty();
                bin.push(arena);
                if (was_empty) {
                    present_arenas.set(step_split_id);
                }
            }
            
            [[nodiscard]] FastArena* _try_extract_cached_arena(FastArena* extracted) {
                CUW3_ASSERT(extracted, "arena was null");

                if (extracted == cached_arena) {
                    cached_arena = nullptr;
                    return extracted;
                }
                return nullptr;
            }

            [[nodiscard]] FastArena* _try_extract_bin_arena(FastArena* extracted, uint64 step_split_id) {
                CUW3_ASSERT(extracted, "arena was null");
                CUW3_ASSERT(step_split_id == 0 || min_step_split_id <= step_split_id && step_split_id < num_step_split_bins, "invalid step_split value");

                if (!FastArenaView{extracted}.in_list()) {
                    return nullptr;
                }

                auto& bin = arenas[step_split_id];
                bin.extract(extracted);
                if (bin.empty()) {
                    present_arenas.unset(step_split_id);
                }
                return extracted;
            }


            // for testing purposes only
            bool _has_any_available_arenas() const {
                return cached_arena || present_arenas.any_set(min_step_split_id);
            }

            // for testing purposes only
            [[nodiscard]] uint64 _sample_largest_bin() const {
                return present_arenas.get_last_set_bit(min_step_split_id);
            }


            // arena selection array of lists helping to choose proper arena to allocate from
            FastArenaBin arenas[step_split_axis] = {};
            // bit mask of non-empty lists
            FastArenaBitmap present_arenas = {};
            // cached arena that we choose to allocate from without scanning arena array
            FastArena* cached_arena = {};

            uint64 alignment{}; // const
            uint64 num_step_split_bins{}; // const
            // min id of the bin that can be used by the algorithm
            uint64 min_step_split_id{}; // const
            // min possible size of the allocation that can be made by any suitable arena
            uint64 min_alloc_size{}; // const
        };


        [[nodiscard]] static FastArenaBins* create(Memory memory, const FastArenaBinsConfig& config) {
            CUW3_CHECK_RETURN_VAL(memory.fits<FastArenaBins>(), nullptr, "invalid memory");

            auto* bins = new (memory.get()) FastArenaBins{};

            auto* bins_info = FastArenaBinsInfo::create(Memory::from(&bins->bins_info), config);
            CUW3_CHECK_RETURN_VAL(bins_info, nullptr, "failed to initialize bins info");

            for (uint alignment_id = 0; alignment_id < bins_info->num_alignments; alignment_id++) {
                auto* entry = FastArenaStepSplitEntry::create(
                    Memory::from(&bins->step_split_entries[alignment_id]),
                    bins_info->get_alignment(alignment_id),
                    bins_info->get_num_step_splits(),
                    bins_info->get_min_step_split_id(alignment_id),
                    bins_info->get_min_alloc_size(alignment_id)
                );
                CUW3_CHECK_RETURN_VAL(entry, nullptr, "failed to initialize step split entry");
            }

            return bins;
        }


        // can_allocate returned true
        [[nodiscard]] AcquiredTypedResource<FastArena> _acquire_cached_arena(uint64 size_aligned, uint64 alignment_id) {
            CUW3_ASSERT(bins_info.check_alignment_id(alignment_id), "invalid alignment id");
            
            return step_split_entries[alignment_id]._acquire_cached_arena(size_aligned);
        }

        // _can_allocate => true
        [[nodiscard]] AcquiredTypedResource<FastArena> _acquire_bin_arena(uint64 size_aligned, uint64 alignment_id) {
            CUW3_ASSERT(bins_info.check_alignment_id(alignment_id), "invalid alignment id");

            auto step_split_id = bins_info.locate_step_split_size_clamped(alignment_id, size_aligned);
            auto& entry = step_split_entries[alignment_id];
            return entry._acquire_bin_arena(step_split_id, size_aligned);
        }

        // returns pointer to the arena that is no longer used
        [[nodiscard]] FastArena* _try_update_cached_arena(FastArena* arena, uint64 alignment_id) {
            CUW3_ASSERT(bins_info.check_alignment_id(alignment_id), "invalid alignment id");

            auto& entry = step_split_entries[alignment_id];
            return entry._try_update_cached_arena(arena);
        }

        // arena is not in any list
        void _put_arena(FastArena* arena, uint64 alignment_id) {
            CUW3_ASSERT(bins_info.check_alignment_id(alignment_id), "invalid alignment");
            
            auto arena_view = FastArenaView{arena};
            CUW3_ASSERT(!arena_view.resettable(), "arena must not be resettable");
            
            auto& entry = step_split_entries[alignment_id];
            auto step_split_id = bins_info.locate_step_split_arena_clamped(alignment_id, arena_view.remaining());
            entry._put_arena(arena, step_split_id);
        }

        void _release_arena(FastArena* arena, uint64 alignment_id) {
            arena = _try_update_cached_arena(arena, alignment_id);
            if (!arena) {
                return;
            }
            _put_arena(arena, alignment_id);
        }

        [[nodiscard]] AcquiredTypedResource<FastArena> _acquire_arena(uint64 size_aligned, uint64 alignment_id) {
            CUW3_ASSERT(bins_info.check_alignment_id(alignment_id), "invalid alignment");

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
            CUW3_CHECK(bins_info.check_alignment_id(alignment_id), "invalid alignment id");

            return step_split_entries[alignment_id]._has_any_available_arenas();
        }

        // for testing purposes only
        bool _has_any_available_arenas() const {
            for (uint alignment_id = 0; alignment_id < bins_info.get_num_alignments(); alignment_id++) {
                if (_has_any_available_arenas(alignment_id)) {
                    return true;
                }
            }
            return false;
        }

        // for testing purposes only
        [[nodiscard]] uint64 _sample_allocation_upper_bound(uint64 alignment_id) const {
            CUW3_CHECK(bins_info.check_alignment_id(alignment_id), "invalid alignment id");
            
            auto& entry = step_split_entries[alignment_id];

            uint64 upper_bound = 0;
            if (entry.cached_arena) {
                upper_bound = FastArenaView{entry.cached_arena}.remaining();
            }

            auto step_split_id = entry._sample_largest_bin();
            if (step_split_id != entry.null_step_split_id) {
                upper_bound = std::max(upper_bound, bins_info.step_split_id_to_info(step_split_id).get_step_split_size());
            }
            return upper_bound;
        }


        // get arena to allocate from
        // arena is removed from the data structure
        // may return null
        [[nodiscard]] AcquiredTypedResource<FastArena> acquire_arena(uint64 size, uint64 alignment) {
            auto alignment_id = bins_info.locate_alignment(alignment);
            if (alignment_id == bins_info.get_num_alignments()) {
                return AcquiredTypedResource<FastArena>::failed();
            }

            uint64 size_aligned = align(size, alignment);
            if (!bins_info.can_allocate_aligned(size_aligned, alignment_id)) {
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
            
            auto alignment_id = bins_info.locate_alignment(arena_view.alignment());
            CUW3_CHECK(bins_info.check_alignment_id(alignment_id), "invalid alignment");
            CUW3_CHECK(arena_view.memory_size() >= step_split_entries[alignment_id].min_alloc_size, "arena is too fucking small");

            _release_arena(arena, alignment_id);
        }

        // arena is guaranteed to be in the data structure
        // we are about to modify arena so internal invariant may become broken
        void extract_arena(FastArena* arena) {
            CUW3_CHECK(arena, "arena was null");

            auto arena_view = FastArenaView{arena};
            auto alignment_id = bins_info.locate_alignment(arena_view.alignment());
            CUW3_CHECK(bins_info.check_alignment_id(alignment_id), "invalid alignment");

            auto& entry = step_split_entries[alignment_id];
            if (entry._try_extract_cached_arena(arena)) {
                return;
            }

            auto step_split_id = bins_info.locate_step_split_arena_clamped(alignment_id, arena_view.remaining());
            if (entry._try_extract_bin_arena(arena, step_split_id)) {
                return;
            }

            CUW3_ABORT("unreachable reached: arena must have ben present in the bins");
        }


        FastArenaStepSplitEntry step_split_entries[align_axis] = {};
        FastArenaBinsInfo bins_info{};
    };

    struct FastArenaStepSplitAllocatorConfig {
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
            return !head;
        }

        FastArena* head{};
    };

    // does not round up size to the min possible allocatable size
    // responsibility to locate owning arena is external to this data structure
    struct FastArenaStepSplitAllocator {
        struct alignas(conf_cacheline) RetiredArenasRoot {
            RetireReclaimEntry entry{};
        };

        struct FastArenaRetireReclaimResourceOps {
            void set_next(void* arena, void* retired_arena_list) {
                ((FastArena*)arena)->retire_reclaim_entry.next = retired_arena_list;
            }
        };


        [[nodiscard]] static FastArenaStepSplitAllocator* create(Memory memory, const FastArenaStepSplitAllocatorConfig& config) {
            CUW3_CHECK_RETURN_VAL(memory.fits<FastArenaStepSplitAllocator>(), nullptr, "fast arena allocator: memory was null");

            auto* allocator = new (memory.get()) FastArenaStepSplitAllocator{};
            auto* bins = FastArenaBins::create(Memory::from(&allocator->fast_arena_bins), config.bins_config);
            CUW3_CHECK_RETURN_VAL(bins, nullptr, "fast arena allocator: failed to create bins");

            auto* retire_reclaim_entry = RetireReclaimEntryView::create(
                Memory::from(&allocator->retired_arenas.entry),
                (RetireReclaimRawPtr)RetireReclaimFlags::RetiredFlag
            );
            CUW3_CHECK_RETURN_VAL(retire_reclaim_entry, nullptr, "fast arena allocator: failed to create retire_reclaim_entry");

            return allocator;
        }


        // for testing purposes only
        [[nodiscard]] uint64 _sample_allocation_upper_bound(uint64 alignment_id) const {
            return fast_arena_bins._sample_allocation_upper_bound(alignment_id);
        }

        // for testing purposes only
        bool _is_allocator_empty() const {
            return !fast_arena_bins._has_any_available_arenas();
        }


        bool supports_alignment(uint64 alignment) const {
            return fast_arena_bins.bins_info.check_alignment(alignment);
        }

        uint64 get_num_alignments() const {
            return fast_arena_bins.bins_info.get_num_alignments();
        }

        uint64 get_min_alignment() const {
            return get_alignment(0);
        }

        uint64 get_max_alignment() const {
            return get_alignment(get_num_alignments() - 1);
        }

        uint64 get_alignment(uint64 alignment_id) const {
            return fast_arena_bins.bins_info.get_alignment(alignment_id);
        }

        uint64 get_min_alloc_size(uint64 alignment_id) const {
            if (alignment_id < get_num_alignments()) {
                return fast_arena_bins.bins_info.get_min_alloc_size(alignment_id);
            }
            return 0;
        }

        uint64 get_max_alloc_size() const {
            return fast_arena_bins.bins_info.get_global_max_alloc_size();
        }

        uint64 get_maxmin_alloc_size() const {
            return fast_arena_bins.bins_info.get_global_maxmin_alloc_size();
        }


        // acquire arena for allocation, may be null
        [[nodiscard]] AcquiredTypedResource<FastArena> acquire_arena(uint64 size, uint64 alignment) {
            return fast_arena_bins.acquire_arena(size, alignment);
        }

        // arena was either previously acquired or has just been created
        // either way arena is not in the data structure
        [[nodiscard]] void* allocate(AcquiredTypedResource<FastArena> arena, uint64 size) {
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
            
            fast_arena_bins.release_arena(arena);
            return allocated;
        }

        // arena is retrieved externally (in some magic way)
        // arena is in the data structure (either cached or in the bins)
        // resets resettable arena and returns it
        [[nodiscard]] FastArena* deallocate(FastArena* arena, void* memory, uint64 size) {
            CUW3_CHECK(arena, "arena was null");
            CUW3_CHECK(memory, "null memory deallocation is not allowed");
            CUW3_CHECK(size, "size was zero");

            // dementia reminder: does not affect remaining() that's why we dont need to rearrange bins!
            auto arena_view = FastArenaView{arena};
            arena_view.release(memory, size);
            if (!arena_view.resettable()) {
                return nullptr;
            }

            // arena became resettable we must extract it from the bins
            fast_arena_bins.extract_arena(arena);
            arena_view.reset();
            return arena;
        }

        // called from the non-owning thread
        // puts retired arenas in the list
        // arena pointer is stored as is - no offsetting will be required on reclaim
        [[nodiscard]] RetireReclaimPtr retire(FastArena* arena, void* memory, uint64 size) {
            CUW3_CHECK(arena, "arena was null");
            CUW3_CHECK(memory, "memory was null");
            CUW3_CHECK(size, "size was zero");

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
            fast_arena_bins.extract_arena(arena);
            
            auto arena_view = FastArenaView{arena};
            arena_view.reclaim_allocations();
            if (arena_view.resettable()) {
                arena_view.reset();
                return arena;
            }

            fast_arena_bins.release_arena(arena);
            return nullptr;
        }

        void postpone(FastArenaReclaimList list) {
            CUW3_CHECK(!retired_arenas.entry.next_postponed, "already postponed");

            retired_arenas.entry.next_postponed = list.head;
        }


        RetiredArenasRoot retired_arenas{};
        FastArenaBins fast_arena_bins{};
    };
}