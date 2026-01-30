#pragma once

#include "conf.hpp"
#include "cuw3/assert.hpp"
#include "list.hpp"
#include "bitmap.hpp"
#include "backoff.hpp"
#include "retire_reclaim.hpp"
#include "region_chunk_handle.hpp"

namespace cuw3 {
    // TODO : some notes about operation

    using FastArenaListEntry = DefaultListEntry;
    using FastArenaListOps = DefaultListOps<FastArenaListEntry>;

    using FastArenaBackoff = SimpleBackoff;

    // TODO : maybe add affinity field (cached, bin, big bin, out of allocator)
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
            list_init(&arena->list_entry, FastArenaListOps{});

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

        bool empty() const {
            return arena->top == 0;
        }

        bool full() const {
            return arena->top == arena->arena_memory_size;
        }

        bool can_allocate(uint64 size) const {
            uint64 rem = remaining();
            CUW3_ASSERT(is_aligned(rem, arena->arena_alignment), "arena internal state misalignment");

            return rem >= align(size, arena->arena_alignment);
        }

        uint64 remaining() const {
            CUW3_ASSERT(arena->arena_memory_size >= arena->top, "top is greater than memory size");

            return arena->arena_memory_size - arena->top;
        }

        uint64 alignment() const {
            return arena->arena_alignment;
        }

        FastArenaListEntry* list_entry() const {
            return &arena->list_entry;
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

    // TODO : algorithm description
    // TODO : static function returning min_alloc & max_alloc
    struct FastArenaBins {
        struct FastArenaBin {
            FastArenaListEntry list_head{};
        };

        static constexpr uint64 align_axis = conf_max_fast_arenas;
        static constexpr uint64 step_axis = conf_max_fast_arena_lookup_steps;
        static constexpr uint64 split_axis = conf_max_fast_arena_lookup_split;
        static constexpr uint64 step_split_axis = (step_axis + 1) * split_axis;

        using FastArenaBitmap = Bitmap<uint64, step_split_axis>;

        struct FastArenaStepSplitEntry {
            // arena selection array of lists helping to choose proper arena to allocate from
            FastArenaBin arenas[step_split_axis] = {};
            // bit mask of non-empty lists
            FastArenaBitmap present_arenas = {};
            // arenas that are too big to fit into the arenas array
            FastArenaBin big_arenas = {};
            // cached arena that we choose to allocate from without scanning arena array
            FastArena* cached_arena = {};
            uint64 cache_misses{};
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

            CUW3_CHECK_RETURN_VAL(config.min_arena_step_size_log2 >= config.num_splits_log2, nullptr, "step size must be bigger than number of splits");

            auto* bins = new (config.memory) FastArenaBins{};
            for (uint alignment_id = 0; alignment_id < align_axis; alignment_id++) {
                auto& entry = bins->step_split_entries[alignment_id];
                for (uint step_split_id = 0; step_split_id <= step_split_axis; step_split_id++) {
                    auto& bin = entry.arenas[step_split_id];
                    list_init(&bin.list_head, FastArenaListOps{});
                }
                list_init(&entry.big_arenas.list_head, FastArenaListOps{});
            }

            bins->num_splits = num_splits;
            bins->num_splits_log2 = config.num_splits_log2;

            bins->num_alignments = num_alignments;
            bins->min_arena_alignment_log2 = config.min_arena_alignment_log2;
            bins->max_arena_alignment_log2 = config.max_arena_alignment_log2;

            bins->num_steps = num_steps;
            bins->min_arena_step_size_log2 = config.min_arena_step_size_log2;
            bins->max_arena_step_size_log2 = config.max_arena_step_size_log2;

            bins->min_alloc_size = intpow2(config.min_arena_step_size_log2 - config.num_splits_log2);
            bins->max_alloc_size = intpow2(config.max_arena_step_size_log2 + 1);
            return bins;
        }


        uint64 locate_alignment(uint64 alignment) {
            uint64 alignment_log2 = intlog2(alignment);
            CUW3_CHECK(alignment_log2 <= max_arena_alignment_log2, "cannot satisfy alignment request");

            alignment_log2 = std::max(alignment_log2, min_arena_alignment_log2);
            return alignment_log2 - min_arena_alignment_log2;
        }

        // size must be aligned beforehand if required
        uint64 locate_step_split(uint64 size) {
            uint64 step_size_log2 = std::min(intlog2(size), max_arena_step_size_log2);

            uint64 step = 0;
            uint64 split_size_base = 0;
            uint64 split_size_log2 = 0;
            if (step_size_log2 < min_arena_step_size_log2) {
                step = 0;
                split_size_base = 0;
                split_size_log2 = min_arena_step_size_log2;
            } else {
                step = step_size_log2 - min_arena_step_size_log2 + 1; // +1 due to zero step
                split_size_base = intpow2(step_size_log2);
                split_size_log2 = step_size_log2;
            }

            // num_splits is pow2, split_size is pow2
            // min((size - base) * num_splits / split_size, num_splits)
            // split can have value that is greater than num_splits - 1
            // this is the case then size is greater than or equal to max_arena_step_size
            // arena must be put into the full list then
            // there is also a special index 0: if arena gets there it is treated as 'being recycled'
            // it means that it is not possible to allocate from it
            // yet it can become free again then is either freed completely or moved into the free list
            uint64 split = std::min(divpow2(mulpow2(size - split_size_base, num_splits_log2), split_size_log2), num_splits);
            uint64 step_split = step * split_axis + split;
            return step_split;
        }

        bool can_allocate(uint64 size) const {
            return min_alloc_size <= size && size <= max_alloc_size;
        }

        [[nodiscard]] FastArena* _acquire_cached_arena(uint64 size, uint64 alignment_id) {
            auto& entry = step_split_entries[alignment_id];
            auto* arena = entry.cached_arena;
            if (FastArenaView{arena}.can_allocate(size)) {
                return std::exchange(entry.cached_arena, nullptr);
            }
            return nullptr;
        }

        [[nodiscard]] FastArena* _acquire_big_arena(uint64 alignment_id) {
            auto& entry = step_split_entries[alignment_id];
            auto& bin = entry.big_arenas;
            if (!list_empty(&bin.list_head, FastArenaListOps{})) {
                return FastArena::list_entry_to_arena(list_pop_head(&bin.list_head, FastArenaListOps{}));
            }
            return nullptr;
        }

        [[nodiscard]] FastArena* _acquire_bin_arena(uint64 size, uint64 alignment_id) {
            auto& entry = step_split_entries[alignment_id];

            auto step_split_id = locate_step_split(size);
            if (step_split_id == step_split_axis) {
                return nullptr; // we need big arena
            }
            CUW3_ASSERT(step_split_id != 0, "attempt to allocate size smaller that minimal");

            auto present_arena_bin_id = entry.present_arenas.get_first_set(step_split_id);
            if (present_arena_bin_id == entry.present_arenas.null_bit) {
                return nullptr;
            }

            auto& bin = entry.arenas[present_arena_bin_id];
            CUW3_ASSERT(!list_empty(&bin.list_head, FastArenaListOps{}), "invariant violation: bit set but list is empty");

            auto* arena = FastArena::list_entry_to_arena(list_pop_head(&bin.list_head, FastArenaListOps{}));
            if (list_empty(&bin.list_head, FastArenaListOps{})) {
                entry.present_arenas.unset(present_arena_bin_id);
            }
            CUW3_ASSERT(FastArenaView{arena}.can_allocate(size), "invariant violation: arena was placed in the improper bin");

            return arena;
        }

        // get arena to allocate from
        // arena is removed from the data structure
        // may return null
        [[nodiscard]] FastArena* acquire_arena(uint64 size, uint64 alignment) {
            CUW3_ASSERT(is_aligned(size, alignment), "size is not properly aligned");
            CUW3_ASSERT(can_allocate(size), "impossible to make arena allocation");

            auto alignment_id = locate_alignment(alignment);
            if (auto* arena = _acquire_cached_arena(size, alignment_id)) {
                return arena;
            }
            if (auto* arena = _acquire_bin_arena(size, alignment_id)) {
                return arena;
            }
            if (auto* arena = _acquire_big_arena(alignment_id)) {
                return arena;
            }
            return nullptr;
        }

        [[nodiscard]] FastArena* _try_update_cached_arena(FastArena* arena, uint64 alignment_id) {
            CUW3_ASSERT(arena, "arena was null");

            auto arena_view = FastArenaView{arena};
            auto& entry = step_split_entries[alignment_id];

            if (!entry.cached_arena) {
                entry.cached_arena = arena;
                return nullptr;
            }

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

        void _put_into_bins(FastArena* arena, uint64 alignment_id) {
            CUW3_ASSERT(arena, "arena was null");

            auto arena_view = FastArenaView{arena};
            auto& entry = step_split_entries[alignment_id];

            auto step_split = locate_step_split(arena_view.remaining());
            if (step_split != step_split_axis) {
                auto& bin = entry.arenas[step_split];
                list_push_head(&bin.list_head, &arena->list_entry, FastArenaListOps{});
                entry.present_arenas.set(step_split);
                return;
            }

            auto& bin = entry.big_arenas;
            list_push_head(&bin.list_head, arena_view.list_entry(), FastArenaListOps{});
        }

        void _release_arena(FastArena* arena, uint64 alignment_id) {
            arena =_try_update_cached_arena(arena, alignment_id);
            if (!arena) {
                return;
            }
            _put_into_bins(arena, alignment_id);
        }

        // return arena back into the data structure
        // returns arena if it is empty
        void release_arena(FastArena* arena) {
            CUW3_CHECK(arena, "arena was null");

            auto arena_view = FastArenaView{arena};
            CUW3_ASSERT(arena_view.resettable(), "arena was empty");

            auto alignment_id = locate_alignment(arena_view.alignment());
            _release_arena(arena, alignment_id);
        }

        // name alias for release_arena
        void put_arena(FastArena* arena) {
            release_arena(arena);
        }

        // arena was either previously acquired or has just been created
        // either way arena is not in the data structure
        [[nodiscard]] void* allocate(FastArena* arena, uint64 size) {
            CUW3_ASSERT(arena, "arena was null");
            CUW3_ASSERT(size, "cannot make zero allocation");

            auto arena_view = FastArenaView{arena};
            CUW3_ASSERT(!arena_view.resettable(), "arena was empty");

            void* allocated = arena_view.acquire(size);
            CUW3_ASSERT(allocated, "arena must have had enough space");
            
            release_arena(arena);
            return allocated;
        }

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
            auto& entry = step_split_entries[alignment_id];
            if (arena == entry.cached_arena) {
                entry.cached_arena = nullptr;
                arena_view.reset();
                return arena;
            }

            auto step_split_id = locate_step_split(arena_view.remaining());
            list_erase(arena_view.list_entry(), FastArenaListOps{}); // common for bin arena and big arena
            if (step_split_id != step_split_axis) {
                if (list_empty(&entry.arenas[step_split_id].list_head, FastArenaListOps{})) {
                    entry.present_arenas.unset(step_split_id); // bin arena only
                }
            }
            arena_view.reset();
            return arena;
        }

        FastArenaStepSplitEntry step_split_entries[align_axis] = {};

        uint64 num_splits{};
        uint64 num_splits_log2{};
        
        uint64 num_steps{};
        uint64 min_arena_step_size_log2{};
        uint64 max_arena_step_size_log2{};
        
        uint64 num_alignments{};
        uint64 min_arena_alignment_log2{};
        uint64 max_arena_alignment_log2{};

        uint64 min_alloc_size{};
        uint64 max_alloc_size{};
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


        // acquire arena for allocation, may be null
        [[nodiscard]] FastArena* acquire_arena(uint64 size, uint64 alignment) {
            return fast_arena_bins.acquire_arena(size, alignment);
        }

        // returns allocated memory, expected to never be null
        [[nodiscard]] void* allocate(FastArena* arena, uint64 size) {
            return fast_arena_bins.allocate(arena, size);
        }

        // returns empty arena or null 
        [[nodiscard]] FastArena* deallocate(FastArena* arena, void* memory, uint64 size) {
            return fast_arena_bins.deallocate(arena, memory, size);
        }

        // TODO : maybe return flags helper instead? Because
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
                return FastArenaReclaimList{(FastArena*)std::exchange(retired_arenas.entry.next_postponed, nullptr)};
            }
            auto retired_arenas_view = RetireReclaimPtrView{&retired_arenas.entry.head};
            return FastArenaReclaimList{retired_arenas_view.reclaim().ptr<FastArena>()};
        }

        void postpone(FastArenaReclaimList list) {
            CUW3_ASSERT(!retired_arenas.entry.next_postponed, "already postponed");

            retired_arenas.entry.next_postponed = list.head;
        }


        RetiredArenasRoot retired_arenas{};
        FastArenaBins fast_arena_bins{};
    };
}