#pragma once

#include "conf.hpp"
#include "list.hpp"
#include "retire_reclaim.hpp"
#include "region_chunk_handle.hpp"

namespace cuw3 {
    using FastArenaListEntry = DefaultListEntry;

    // resource hierarchy
    // allocation -> arena -> region list -> root (thread)
    struct FastArena {
        struct alignas(conf_cacheline) {
            RegionChunkHandleHeader handle_header{};
            FastArenaListEntry list_entry{};

            uint32 alignment{};
            uint32 allocated{};
            uint32 remaining{};

            void* arena_top{};
            void* arena_memory{};
        };

        struct alignas(conf_cacheline) {
            RetireReclaimEntry retire_reclaim_entry{};
            // nothing to postpone
        };
    };

    static_assert(sizeof(FastArena) <= conf_control_block_size, "pack struct field better or increase size of the control block");

    // TODO : do the fucking implementation
    struct FastArenaConfig {
        
    };

    struct FastArenaView {


        FastArena* arena{};
    };
}