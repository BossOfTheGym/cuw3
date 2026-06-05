#include "cuw3/conf.hpp"
#include "cuw3/defs.hpp"
#include "cuw3/export.hpp"

#include "cuw3/vmem.hpp"
#include "cuw3/allocator.hpp"
#include "cuw3/region_chunk_allocator.hpp"
#include "cuw3/thread_local_allocator.hpp"

using namespace cuw3;

// many tests and benchmarks rely on the consts used here
// so be sure to change themas well or too provide appropriate means to query necessary info
namespace {
    inline constexpr uint64 cuw3_try_reclaim_each_op = 8;
    inline constexpr uint64 cuw3_try_cleanup_each_op = 16;

    RegionChunkAllocatorSpecsConfig cuw3_create_rca_alloc_specs_config() {
        RegionChunkAllocatorSpecsConfig config{};
        config.region_sizes = conf_region_sizes_log2;
        config.num_region_sizes = conf_num_region_sizes;
        config.region_chunk_sizes = conf_region_chunk_sizes_log2;
        config.num_region_chunk_sizes = conf_num_region_chunk_sizes;
        config.handle_size = conf_region_handle_size;
        config.handle_alignment = conf_cacheline;
        config.region_alignment = std::max<uint64>({vmem_huge_page_size(), conf_min_region_chunk_size, intpow2(conf_max_alignment_log2)});
        return config;
    }

    AllocatorConfig cuw3_create_allocator_config() {
        AllocatorConfig config{};
        config.rca_specs_config = cuw3_create_rca_alloc_specs_config();
        config.contention_split = conf_max_contention_split;
        config.num_grave_entries = conf_graveyard_slot_count;
        return config;
    }

    [[nodiscard]] cuw3::Allocator* cuw3_create_allocator() {
        auto config = cuw3_create_allocator_config();
        uint64 alloc_size = sizeof(cuw3::Allocator);
        auto* alloc_mem = vmem_alloc(alloc_size, VMemAllocType::VMemReserveCommit);
        if (!alloc_mem) {
            return nullptr;
        }

        auto* alloc = Allocator::create(Memory::from(alloc_mem, alloc_size), config);
        if (!alloc) {
            vmem_free(alloc_mem, alloc_size);
            return nullptr;
        }
        CUW3_UNPOISON_MEMORY_REGION(alloc_mem, alloc_size);
        return alloc;
    }

    [[nodiscard]] cuw3::Allocator* cuw3_get_allocator() {
        static Allocator* allocator = cuw3_create_allocator();
        return allocator;
    }


    cuw3::FastArenaSmallAllocatorConfig cuw3_create_fast_arena_small_alloc_config() {
        cuw3::FastArenaSmallAllocatorConfig config{};
        config.bins_config.min_arena_alignment_log2 = conf_fast_arena_min_alignment_log2;
        config.bins_config.max_arena_alignment_log2 = conf_fast_arena_max_alignment_log2;
        config.bins_config.size_cutoff = conf_size_cutoff;
        return config;
    }

    cuw3::FastArenaStepSplitAllocatorConfig cuw3_create_fast_arena_step_split_alloc_config() {
        cuw3::FastArenaStepSplitAllocatorConfig config{};
        config.bins_config.num_splits_log2 = intlog2(conf_max_fast_arena_lookup_split);
        config.bins_config.min_arena_step_size_log2 = conf_max_region_chunk_size_log2 - CUW3_MAX_FAST_ARENA_LOOKUP_STEPS;
        config.bins_config.max_arena_step_size_log2 = conf_max_region_chunk_size_log2 - 1;
        config.bins_config.min_arena_alignment_log2 = conf_fast_arena_min_alignment_log2;
        config.bins_config.max_arena_alignment_log2 = conf_fast_arena_max_alignment_log2;
        return config;
    }

    cuw3::ThreadLocalAllocatorConfig cuw3_create_tla_config(uint64 thread_id) {
        cuw3::ThreadLocalAllocatorConfig config{};
        config.small_alloc_config = cuw3_create_fast_arena_small_alloc_config();
        config.step_split_alloc_config = cuw3_create_fast_arena_step_split_alloc_config();
        config.thread_id = thread_id;
        return config;
    }

    [[nodiscard]] cuw3::ThreadLocalAllocator* cuw3_create_new_tla() {
        auto* alloc = cuw3_get_allocator();
        if (!alloc) {
            return nullptr;
        }

        uint64 tla_size = sizeof(cuw3::ThreadLocalAllocator);
        uint64 tla_alignment = std::max<uint64>(region_owner_alignment, vmem_page_size());

        void* tla_mem = vmem_alloc_aligned(tla_size, VMemAllocType::VMemReserveCommit, tla_alignment);
        if (!tla_mem) {
            return nullptr;
        }
        
        auto config = cuw3_create_tla_config(alloc->acquire_thread_id());
        auto* tla = cuw3::ThreadLocalAllocator::create(Memory::from(tla_mem, tla_size), config);
        if (!tla) {
            vmem_free(tla_mem, tla_size);
            return nullptr;
        }
        CUW3_UNPOISON_MEMORY_REGION(tla, tla_size);

        return tla;
    }

    [[nodiscard]] cuw3::ThreadLocalAllocator* cuw3_create_tla() {
        auto* alloc = cuw3_get_allocator();
        if (!alloc) {
            return nullptr;
        }
        auto* tla = alloc->snatch_dead_tla();
        if (tla) {
            return tla;
        }
        return cuw3_create_new_tla();
    }

    void cuw3_destroy_tla(cuw3::ThreadLocalAllocator* tla) {
        uint64 tla_size = sizeof(cuw3::ThreadLocalAllocator);
        CUW3_POISON_MEMORY_REGION(tla, tla_size);
        vmem_free(tla, tla_size);
    }

    struct ThreadLocalAllocatorGuard {
        ~ThreadLocalAllocatorGuard() {
            auto* alloc = cuw3_get_allocator();
            CUW3_CHECK_CRITICAL(alloc, "allocator was nullptr");
            if (tla) {
                if (alloc->reclaim(tla)) {
                    cuw3_destroy_tla(tla);
                } else {
                    alloc->retire_dead_tla(tla);
                }
            }
            tla = nullptr;
        }

        cuw3::ThreadLocalAllocator* tla{};
    };

    cuw3::ThreadLocalAllocator* cuw3_get_tla() {
        static thread_local ThreadLocalAllocatorGuard guard{cuw3_create_tla()};
        return guard.tla;
    }
}

extern "C" {
    CUW3_API void* cuw3_alloc(uint64_t size, uint64_t alignment) {
        auto* alloc = cuw3_get_allocator();
        if (!alloc) {
            return nullptr;
        }
        auto* tla = cuw3_get_tla();
        if (!tla) {
            return nullptr;
        }
        auto res = alloc->allocate(tla, size, alignment);
        if (res.status_acquired()) {
            return res.get();
        }
        return nullptr;
    }

    CUW3_API void cuw3_free(void* ptr, uint64_t size) {
        auto* alloc = cuw3_get_allocator();
        if (!alloc) {
            return;
        }
        auto* tla = cuw3_get_tla();
        if (!tla) {
            return;
        }

        alloc->deallocate(tla, ptr, size);

        tla->extended_free_counter--;
        if (tla->extended_free_counter % cuw3_try_reclaim_each_op == 0) {
            alloc->reclaim(tla);
        }
        if (tla->extended_free_counter % cuw3_try_cleanup_each_op == 0) {
            if (auto* released = alloc->do_tla_cleanup(tla)) {
                cuw3_destroy_tla(released);
            }
        }
    }

    CUW3_API void cuw3_reclaim() {
        auto* alloc = cuw3_get_allocator();
        if (!alloc) {
            return;
        }
        auto* tla = cuw3_get_tla();
        if (!tla) {
            return;
        }
        alloc->reclaim(tla);
    }

    CUW3_API void cuw3_cleanup() {
        auto* alloc = cuw3_get_allocator();
        if (!alloc) {
            return;
        }
        auto* tla = cuw3_get_tla();
        if (!tla) {
            return;
        }
        if (auto* released = alloc->do_tla_cleanup(tla)) {
            cuw3_destroy_tla(released);
        }
    }
}