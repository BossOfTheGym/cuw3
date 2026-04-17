
#pragma once
#include "cuw3/utils.hpp"
#include "vmem.hpp"
#include "thread_graveyard.hpp"
#include "region_chunk_allocator.hpp"
#include "thread_local_allocator.hpp"

namespace cuw3 {
    // rca = region chunk allocator
    // tla = thread local allocator

    struct AllocatorConfig {
        RegionChunkAllocatorSpecsConfig rca_specs_config{};
        uint64 contention_split{};
        uint64 num_grave_entries{};
    };

    struct AllocatorMemoryBundle {
        [[nodiscard]] static AllocatorMemoryBundle from(const RegionChunkAllocatorSpecs& specs) {
            AllocatorMemoryBundle bundle{};
            bundle.regions = vmem_alloc_aligned(specs.total_regions_size, VMemAllocType::VMemReserve, specs.region_alignment);
            if (!bundle.regions) {
                goto failed_regions_alloc;
            }
            bundle.handles = vmem_alloc_aligned(specs.total_handles_size, VMemAllocType::VMemReserveCommit, specs.handle_alignment);
            if (!bundle.handles) {
                goto failed_handles_alloc;
            }
            bundle.pool_handles = vmem_alloc_aligned(specs.total_pool_handles_size, VMemAllocType::VMemReserveCommit, specs.pool_handles_alignment);
            if (!bundle.pool_handles) {
                goto failed_pool_handles_alloc;
            }
            return bundle;

        failed_pool_handles_alloc:
            vmem_free(bundle.handles, specs.total_handles_size);
        failed_handles_alloc:
            vmem_free(bundle.regions, specs.total_regions_size);
        failed_regions_alloc:
            return {};
        }

        static void release(AllocatorMemoryBundle bundle, const RegionChunkAllocatorSpecs& specs) {
            if (!bundle) {
                return;
            }
            vmem_free(bundle.regions, specs.total_regions_size);
            vmem_free(bundle.handles, specs.total_handles_size);
            vmem_free(bundle.pool_handles, specs.total_pool_handles_size);
        }

        explicit operator bool() const {
            return regions && handles && pool_handles;
        }

        void* regions{};
        void* handles{};
        void* pool_handles{};
    };

    struct Allocator {
        [[nodiscard]] static Allocator* create(Memory memory, const AllocatorConfig& config) {
            CUW3_CHECK_RETURN_VAL(memory.fits<Allocator>(), nullptr, "allcotor: invalid memory");
            
            auto* alloc = new (memory.get()) Allocator{};

            auto* rca_specs = RegionChunkAllocatorSpecs::create(Memory::from(&alloc->rca_specs), config.rca_specs_config);
            CUW3_CHECK_RETURN_VAL(rca_specs, nullptr, "allocator: failed to initialize region chunk allocator specs");

            RegionChunkAllocatorPoolsConfig rca_pools_config{};
            rca_pools_config.specs = rca_specs;
            rca_pools_config.contention_split = config.contention_split;
            auto* rca_pools = RegionChunkAllocatorPools::create(Memory::from(&alloc->rca_pools), rca_pools_config);
            CUW3_CHECK_RETURN_VAL(rca_pools, nullptr, "allocator: failed to initialize region chunk allocator pools");

            auto memory_bundle = AllocatorMemoryBundle::from(*rca_specs);
            CUW3_CHECK_RETURN_VAL(memory_bundle, nullptr, "allocator: failed to allocate memory");

            RegionAllocatorConfig rca_config{};
            rca_config.specs = rca_specs;
            rca_config.pools = rca_pools;
            rca_config.regions = memory_bundle.regions;
            rca_config.handles = memory_bundle.handles;
            rca_config.pool_handles = memory_bundle.pool_handles;

            auto* rca = RegionChunkAllocator::create(Memory::from(&alloc->rca), rca_config);
            auto* tla_graveyard = ThreadGraveyard::create(Memory::from(&alloc->tla_graveyard), config.num_grave_entries);
            CUW3_CHECK_GOTO(rca, free_alloc_memory, "allocator: failed to initialize region chunk allocator");
            CUW3_CHECK_GOTO(tla_graveyard, free_alloc_memory, "allocator: failed to initialize thread graveyard");

            return alloc;

        free_alloc_memory:
            AllocatorMemoryBundle::release(memory_bundle, *rca_specs);
            return nullptr;
        }

        static void destroy(Allocator* alloc) {
            CUW3_CHECK(alloc, "allocator: alloc was null on release");

            AllocatorMemoryBundle::release({alloc->rca.regions, alloc->rca.handles, alloc->rca.pool_handles}, alloc->rca_specs);
        }

        // region chunk allocation alg
        // we should somehow decide how fucking big chunk should be when we need one
        // simple answer(as I forgot about this)! we will keep track of total amount of allocated chunks... at least
        // we definitely have to allocate chunk big enough to serve an allocation request (if that possible)
        // we can take total usage into account and use it as a minimal chunk size to serve further requests better
        // or we can just omit this completely and rely only on the size of the allocation
        // but what if we run out of some chunk size?
        // then we have to use bigger one!
        // what if the chunk of some particular size that we chose according to total size usage is not available?
        // then we ... hmmm... scan the region sizes starting from the first satisfying until we manage to allocate the chunk!
        // yay
        // we live yet another day!
        // dis is stonks

        [[nodiscard]] RegionChunkAllocation _allocate_chunk(ThreadLocalAllocator* tla, uint64 size, uint64 alignment) {
            uint64 demand = std::max(
                align(size, alignment), 
                std::min(rca.get_max_chunk_size(), tla->total_chunk_storage_size)
            );
            uint32 region = rca.search_suitable_region(demand);
            if (region == region_chunk_allocator_null_value) {
                return {};
            }

            // TODO : we work with internal tla data here, do something with it later
            RegionChunkAllocParams alloc_params{};
            alloc_params.rounds = 4;
            alloc_params.attempts = -1;
            alloc_params.split_step = 1;
            for (uint32 curr_region = region; curr_region < rca.get_num_regions(); curr_region++) {
                alloc_params.split_start = tla->last_chunk_pool_split_id[curr_region];
                if (auto chunk_allocation = rca.allocate_chunk(curr_region,  alloc_params)) {
                    tla->last_chunk_pool_split_id[curr_region] = chunk_allocation.split; // update split
                    tla->total_chunk_storage_size += rca.get_region_spec(curr_region).get_chunk_size(); // pump up the usage
                    return chunk_allocation;
                }
            }
            return {};
        }

        [[nodiscard]] FastArena* _construct_arena(ThreadLocalAllocator* tla, RegionChunkAllocation chunk_allocation, uint64 alignment, uint64 type) {
            // TODO : no_check can be used

            RegionChunkMemory chunk_memory{};
            uint64 chunk_size{};
            FastArenaConfig config{};
            FastArena* arena{};

            chunk_memory = rca.region_data_to_memory(chunk_allocation.region, chunk_allocation.chunk, chunk_allocation.handle);;
            if (!chunk_memory) {
                goto failed_to_allocate_chunk;
            }
            
            // TODO : stupid as hell, but I'm gonna put somewhere else later
            chunk_size = rca.get_region_spec(chunk_allocation.region).get_chunk_size();

            // TODO : check return value
            // TODO : check resource destruction order
            if (!vmem_commit(chunk_memory.chunk, chunk_size)) {
                goto failed_to_commit;
            }

            config.owner = tla;
            config.arena_type = type;
            config.arena_alignment = alignment;
            config.arena_memory = chunk_memory.chunk;
            config.arena_memory_size = chunk_size;
            config.retire_reclaim_flags = (uint64)RetireReclaimFlags::RetiredFlag;

            arena = FastArenaView::create(Memory::from(chunk_memory.handle), config);
            if (arena) {
                return arena;
            }
            // fallthrough

        failed_to_commit:
            rca.deallocate_chunk(chunk_memory);
        failed_to_allocate_chunk:
            return nullptr;
        }

        [[nodiscard]] FastArena* _construct_small_allocator_arena(ThreadLocalAllocator* tla, RegionChunkAllocation chunk_allocation, uint64 alignment) {
            return _construct_arena(tla, chunk_allocation, alignment, (uint64)RegionChunkType::FastArenaSmallAllocator);
        }

        [[nodiscard]] FastArena* _construct_step_split_arena(ThreadLocalAllocator* tla, RegionChunkAllocation chunk_allocation, uint64 alignment) {
            return _construct_arena(tla, chunk_allocation, alignment, (uint64)RegionChunkType::FastArenaStepSplitAllocator);
        }

        [[nodiscard]] FastArena* _acquire_new_arena(ThreadLocalAllocator* tla, uint64 size, uint64 alignment, uint64 arena_type) {
            auto chunk_allocation = _allocate_chunk(tla, size, alignment);
            if (!chunk_allocation) {
                return nullptr;
            }
            auto* arena = _construct_arena(tla, chunk_allocation, alignment, arena_type);
            CUW3_CHECK(arena, "failed to construct small arena");
            return arena;
        }

        [[nodiscard]] AcquiredResource _allocate_small_allocator(ThreadLocalAllocator* tla, uint64 size, uint64 alignment) {
            auto acquired_res = tla->small_allocator.acquire(size, alignment);
            if (acquired_res.status_no_resource()) {
                auto* arena = _acquire_new_arena(tla, size, alignment, (uint64)RegionChunkType::FastArenaSmallAllocator);
                if (!arena) {
                    return AcquiredResource::no_resource();
                }
                return AcquiredResource::acquired(tla->small_allocator.allocate(arena, size));
            }
            if (acquired_res.status_acquired()) {
                return AcquiredResource::acquired(tla->small_allocator.allocate(acquired_res.get(), size));
            }
            return AcquiredResource::failed();
        }

        // TODO : this seems to damn like _allocate_small_allocator
        [[nodiscard]] AcquiredResource _allocate_step_split_allocator(ThreadLocalAllocator* tla, uint64 size, uint64 alignment) {
            auto acquired_res = tla->step_split_allocator.acquire_arena(size, alignment);
            if (acquired_res.status_no_resource()) {
                auto* arena = _acquire_new_arena(tla, size, alignment, (uint64)RegionChunkType::FastArenaStepSplitAllocator);
                if (!arena) {
                    return AcquiredResource::no_resource();
                }
                return AcquiredResource::acquired(tla->step_split_allocator.allocate(arena, size));
            }
            if (acquired_res.status_acquired()) {
                return AcquiredResource::acquired(tla->step_split_allocator.allocate(acquired_res.get(), size));
            }
            return AcquiredResource::failed();
        }

        [[nodiscard]] AcquiredResource allocate(ThreadLocalAllocator* tla, uint64 size, uint64 alignment) {
            // TODO : alg
            // find what size category does this allocation fall too (whta allocator to use)
            // try to acquire appropriate arena
            // try to allocate fresh arena if nothing was received
            // allocate from the arena
            // put it back
            if (size < tla->small_allocator.get_size_cutoff()) {
                return _allocate_small_allocator(tla, size, alignment);
            }
            return _allocate_step_split_allocator(tla, size, alignment);
        }

        struct DeallocationContext {
            RegionChunkAllocation chunk_allocation{};
            RegionChunkMemory chunk_memory{};
            FastArena* arena{};
            ThreadLocalAllocator* arena_tla{};
        };

        // TODO : decommit this motherfucker
        void _destroy_arena(ThreadLocalAllocator* tla, FastArena* arena) {
            // TODO : view? or maybe allow simple access to the fields
            // TODO : check return value
            // TODO : check resource fucking acquisition
            // TODO : check order
            // TODO : check! check! check! check! whole night! you trapped me motherfucker!
            vmem_decommit(arena->arena_memory, arena->arena_memory_size);
            tla->total_chunk_storage_size -= arena->arena_memory_size;
            rca.deallocate_chunk({arena->arena_memory, arena});
        }

        void _deallocate(ThreadLocalAllocator* tla, const DeallocationContext& context, void* ptr, uint64 size) {
            FastArena* released_arena{};
            auto type = FastArenaView{context.arena}.type();
            if (type == (uint64)RegionChunkType::FastArenaSmallAllocator) {
                released_arena = tla->small_allocator.deallocate(context.arena, ptr, size);                
            } else if (type == (uint64)RegionChunkType::FastArenaStepSplitAllocator) {
               released_arena = tla->step_split_allocator.deallocate(context.arena, ptr, size);                
            } else {
                CUW3_ABORT("Invalid arena type detected");
            }
            if (released_arena) {
                _destroy_arena(tla, released_arena);
            }
        }

        // we dont care here about the retire-reclaim flags
        // we could have cared, in fact and it would have made our life kind of ... easier?
        void _deallocate_non_owner(ThreadLocalAllocator* tla, const DeallocationContext& context, void* ptr, uint64 size) {
            auto type = FastArenaView{context.arena}.type();
            if (type == (uint64)RegionChunkType::FastArenaSmallAllocator) {
                (void)context.arena_tla->small_allocator.retire(context.arena, ptr, size);
            } else if (type == (uint64)RegionChunkType::FastArenaStepSplitAllocator) {
                (void)context.arena_tla->step_split_allocator.retire(context.arena, ptr, size);
            } else {
                CUW3_ABORT("Invalid arena type detected");
            }
        }

        void deallocate(ThreadLocalAllocator* tla, void* ptr, uint64 size) {
            // locate the handle
            // find the type of the allocator and its owner (tla)
            // find if current thread owns the allocation or not
            // deallocate if it own. recycle the allocator if necessary
            // retire if we dont own it
            auto chunk_allocation = rca.ptr_to_allocation(ptr);
            CUW3_CHECK(chunk_allocation, "attempt to deallocate invalid pointer");

            auto chunk_memory = rca.region_data_to_memory(chunk_allocation.region, chunk_allocation.chunk, chunk_allocation.handle);
            auto* arena = (FastArena*)chunk_memory.handle;
            auto arena_view = FastArenaView{arena};
            auto* arena_tla = (ThreadLocalAllocator*)arena_view.owner();
            auto context = DeallocationContext{chunk_allocation, chunk_memory, arena, arena_tla};
            if (arena_tla == tla) {
                _deallocate(tla, context, ptr, size);
            }
            _deallocate_non_owner(arena_tla, context, ptr, size);
        }

        // TODO : these two functions look the same
        void _reclaim_small_allocator(ThreadLocalAllocator* tla) {
            auto reclaim_list = tla->small_allocator.reclaim_arenas();
            while (reclaim_list) {
                auto* arena = reclaim_list.pop();
                auto* released_arena = tla->small_allocator.reclaim_arena(arena);
                if (released_arena) {
                    _destroy_arena(tla, released_arena);
                }
            }
        }

        void _reclaim_step_split_allocator(ThreadLocalAllocator* tla) {
            auto reclaim_list = tla->step_split_allocator.reclaim_arenas();
            while (reclaim_list) {
                auto* arena = reclaim_list.pop();
                auto* released_arena = tla->step_split_allocator.reclaim_arena(arena);
                if (released_arena) {
                    _destroy_arena(tla, released_arena);
                }
            }
        }

        // NOTE: can be made smarter. We can limit reclamation amount. But guess what? I don't want to implement this any longer!
        void reclaim(ThreadLocalAllocator* tla) {
            _reclaim_small_allocator(tla);
            _reclaim_step_split_allocator(tla);
        }


        // retirement, bitch!
        // after thread is done its thread-local allocator can only go to the .... graveyard
        // damn it! I have to know if thread-local allocator is empty and can be just erased from existence
        // we cannot foster chunk from the dead thread (in general case)
        // in particular, we can! we can attempt to retire some arena and if we are successful then there no threads around
        // we can foster arena only if we detect that this arena has not been retired by anybody
        // this can serve as a guarantee
        // in this case we can reassign the owner
        // but this would not happen often and I dont genuinely want to implement this
        // complication: fast arena bins
        // another one: we have to maintain global list of arenas if we want to manipulate them
        // this should not happen too often so I should not probably care
        // let it just hang in there until fully recycled
        [[nodiscard]] ThreadGraveData acquire_dead_tla() {
            // TODO : alg
            return {};
        }

        void release_dead_tla(ThreadGraveData tla) {
            // TODO : alg
        }

        void put_back_dead_tla(ThreadGraveData tla) {
            // TODO : alg
        }

        void retire_dead_tla(ThreadLocalAllocator* tla) {
            // TODO : alg
        }


        RegionChunkAllocatorSpecs rca_specs{};
        RegionChunkAllocatorPools rca_pools{};
        RegionChunkAllocator rca{};
        ThreadGraveyard tla_graveyard{};
    };
}