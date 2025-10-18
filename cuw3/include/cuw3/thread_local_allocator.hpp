#pragma once

#include "conf.hpp"
#include "list.hpp"
#include "backoff.hpp"
#include "retire_reclaim.hpp"

#define CUW3_GRAVEYARD_SLOT_COUNT 16

namespace cuw3 {
    inline constexpr usize conf_graveyard_slot_count = CUW3_GRAVEYARD_SLOT_COUNT;

    // grave consists of slots and common retire list
    // first, you attempt to fill the slots, after that (if you failed) you push it to the common list
    
    using ThreadGraveyardBackoff = SimpleBackoff;

    using ThreadGraveSlotRawPtr = uint64;

    inline constexpr ThreadGraveSlotRawPtr thread_grave_status_bits = 1;

    enum ThreadGraveSlotFlags : ThreadGraveSlotRawPtr {
        // when set, means that somebody acquired this slot
        // distributing thread must not put any threads here as it may be occupied back
        // if slot is not occupied and empty => it is just empty
        Acquired = 1,
    };

    using ThreadGravePtr = AlignmentPackedPtr<ThreadGraveSlotRawPtr, 1>;


    struct alignas(conf_cacheline) ThreadGrave {
        ThreadGravePtr extremely_dead_motherfucker{};
    };

    struct ThreadGraveObtained {
        void* extremely_dead_motherfucker{};
        uint32 grave_num{};
    };

    struct ThreadGraveyard {
        ThreadGrave thread_graves[conf_graveyard_slot_count] = {};

        struct alignas(conf_cacheline) {
            void* dead_body_dump{};
        };
    };

    struct ThreadLocalAllocator {
        // TODO
    };
}