#pragma once

#include "conf.hpp"
#include "list.hpp"
#include "retire_reclaim.hpp"

#define CUW3_GRAVEYARD_SLOT_COUNT 8

namespace cuw3 {
    inline constexpr usize conf_graveyard_slot_count = CUW3_GRAVEYARD_SLOT_COUNT;

    struct ThreadLocalAllocator;

    struct alignas(conf_cacheline) ThreadLocalAllocatorGraveyardSlot {
        uint64 count{};
        void* head{};
    };

    struct ThreadLocalAllocatorGraveyard {
        ThreadLocalAllocatorGraveyardSlot slots[conf_graveyard_slot_count] = {};
    };

    struct ThreadLocalAllocator {
        // TODO
    };
}