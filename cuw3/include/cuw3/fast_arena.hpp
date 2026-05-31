#pragma once

#include "conf.hpp"
#include "cuw3/defs.hpp"
#include "list.hpp"
#include "utils.hpp"
#include "bitmap.hpp"
#include "assert.hpp"
#include "backoff.hpp"
#include "retire_reclaim.hpp"
#include "region_chunk_handle.hpp"

namespace cuw3 {
    // NOTE : fast arenas can allocate even for bigger alignments at a cost of wating little big of memory
    // algorithm is the following:
    // 1. we allocate-free (just inc freed value) memory that precedes the alignment boundary
    // 2. we make an allocation if there is enough space
    using FastArenaListEntry = DefaultListEntry;
    using FastArenaListOps = DefaultListOps<FastArenaListEntry>;

    using FastArenaBackoff = SimpleBackoff;

    // THINK : something must be done with view and const-view issue. Basically, when we want to provide some const-correctness.
    // * kind of solved: whatever the const view can do  general view can do too, so general view inherits from the const one
    // THINK : do something with the cache alignment issue (make it more convenient)
    // resource hierarchy
    // allocation -> arena -> (???)
    // NOTE : we can apply here location hints
    struct alignas(conf_cacheline) FastArena {
        static FastArena* list_entry_to_arena(FastArenaListEntry* list_entry) {
            return cuw3_field_to_obj(list_entry, FastArena, list_entry);
        }

        CUW3_NEW_CACHELINE // least volatile data)
        RegionChunkHandleHeader region_chunk_header{};
        RetireReclaimEntry retire_reclaim_entry{};

    #ifdef CUW3_ENABLE_DEBUG_CODE
        uint64 debug_label{};
    #endif

        CUW3_NEW_CACHELINE // most volatile data
        FastArenaListEntry list_entry{};
        
        uint64 freed{};
        uint64 top{};
        uint64 arena_memory_size{};
        uint64 arena_alignment{};
        
        void* arena_memory{};
    };

    static_assert(sizeof(FastArena) <= conf_control_block_size, "pack struct field better or increase size of the control block");


    struct FastArenaConfig {
        void* owner{};
        uint64 arena_type{};

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
            arena->region_chunk_header = RegionChunkHandleHeader::from(config.owner, config.arena_type);

            arena->arena_alignment = config.arena_alignment;
            arena->top = 0;
            arena->arena_memory_size = config.arena_memory_size;
            arena->arena_memory = config.arena_memory;

            auto* retire_reclaim_entry = RetireReclaimEntryView::create(
                Memory::from(&arena->retire_reclaim_entry),
                config.retire_reclaim_flags,
                config.arena_type,
                offsetof(FastArena, retire_reclaim_entry)
            );
            CUW3_CHECK_RETURN_VAL(retire_reclaim_entry, nullptr, "fast_arena: failed to create retire_reclaim_entry");

            return arena;
        }

        [[nodiscard]] static FastArenaView create_view(Memory memory, const FastArenaConfig& config) {
            return {create(memory, config)};
        }


        [[nodiscard]] void* acquire(uint64 size) {
            CUW3_CHECK(arena->arena_memory_size >= arena->top, "top is greater than memory size");

            uint64 old_top = arena->top;
            uint64 remaining = arena->arena_memory_size - arena->top;
            uint64 required_space = align(size, arena->arena_alignment);
            if (remaining < required_space) {
                return nullptr;
            }
            arena->top += required_space;

            void* mem = advance_ptr(arena->arena_memory, old_top);
            CUW3_UNPOISON_MEMORY_REGION(mem, required_space);
            return mem;
        }

        void release_reclaimed(uint64 size) {
            CUW3_CHECK(is_aligned(size, alignment()), "size is not aligned");

            uint64 new_freed = arena->freed + size;
            CUW3_CHECK(new_freed <= arena->top, "we have freed more than allocated");

            arena->freed = new_freed;
        }

        void release(void* memory, uint64 size) {
            release_aligned(memory, align(size, arena->arena_alignment));
        }

        void release_aligned(void* memory, uint64 size) {
            CUW3_POISON_MEMORY_REGION(memory, size);

            CUW3_CHECK(is_aligned(size, alignment()), "size is not aligned");
            CUW3_CHECK(has_memory_range(memory, size), "memory does not belong to the arena");

            uint64 new_freed = arena->freed + size;
            CUW3_CHECK(new_freed <= arena->top, "we have freed more than allocated");

            arena->freed = new_freed;
        }

        void reset() {
            CUW3_CHECK(resettable(), "arena was not resettable");
            
            arena->top = 0;
            arena->freed = 0;

            CUW3_POISON_MEMORY_REGION(arena->arena_memory, arena->arena_memory_size);
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
            CUW3_CHECK(is_aligned(size_aligned, arena->arena_alignment), "misaligned size");

            uint64 rem = remaining();
            CUW3_CHECK(is_aligned(rem, arena->arena_alignment), "arena internal state misalignment");

            return rem >= size_aligned;
        }

        bool can_allocate(uint64 size) const {
            return can_allocate_aligned(align(size, arena->arena_alignment));
        }

        uint64 memory_size() const {
            return arena->arena_memory_size;
        }

        uint64 remaining() const {
            CUW3_CHECK(arena->arena_memory_size >= arena->top, "top is greater than memory size");

            return arena->arena_memory_size - arena->top;
        }

        uint64 alignment() const {
            return arena->arena_alignment;
        }

        uint64 type() const {
            return arena->region_chunk_header.data();
        }

        void* owner() const {
            return arena->region_chunk_header.owner();
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
            CUW3_POISON_MEMORY_REGION(memory, align(size, arena->arena_alignment));

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

        explicit operator bool() const {
            return !empty();
        }

        FastArena* head{};
    };

    struct alignas(conf_cacheline) FastArenaRetiredArenasRoot {
        RetireReclaimEntry entry{};
    };

    struct FastArenaRetireReclaimResourceOps {
        void set_next(void* arena, void* retired_arena_list) {
            ((FastArena*)arena)->retire_reclaim_entry.next = retired_arena_list;
        }
    };

    // NOTE : there is possibility for generalization Code of both allocators looks almost the same
    // NOTE : retire-reclaim part really looks similar for both allocators
    // NOTEs : allocator can be really refactored into: allocator + arena size data structure
}