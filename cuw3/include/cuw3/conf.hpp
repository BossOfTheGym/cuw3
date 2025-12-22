#pragma once

#include "defs.hpp"
#include "funcs.hpp"

namespace cuw3 {
    inline constexpr uint64 conf_cacheline = CUW3_CACHELINE_SIZE;

    inline constexpr uint64 conf_control_block_size = align(std::max<uint64>(CUW3_CONTROL_BLOCK_SIZE, 2 * conf_cacheline), conf_cacheline);
    inline constexpr uint64 conf_region_handle_size = conf_control_block_size;
    inline constexpr uint64 conf_control_block_size_log2 = intlog2(conf_control_block_size);

    inline constexpr uint64 conf_min_alloc_size = CUW3_MIN_ALLOC_SIZE; // TODO : checks
    inline constexpr uint64 conf_min_alloc_alignment = CUW3_MIN_ALLOC_ALIGNMENT; // TODO : checks

    inline constexpr usize conf_max_region_sizes = CUW3_MAX_REGION_SIZES;
    inline constexpr gsize conf_num_regions = conf_max_region_sizes;

    inline constexpr uint64 conf_region_sizes_log2[] = {CUW3_REGION_SIZES_LOG2};
    inline constexpr usize conf_num_region_sizes = array_size(conf_region_sizes_log2);

    static_assert(all_sizes_valid(conf_region_sizes_log2), "all region sizes must be less than 40");
    static_assert(conf_num_region_sizes <= conf_max_region_sizes);

    using RegionSizeArray = std::array<uint64, conf_num_region_sizes>;
    
    inline constexpr RegionSizeArray conf_region_sizes_array = {CUW3_REGION_SIZES_LOG2};

    inline constexpr uint64 conf_region_chunk_sizes_log2[] = {CUW3_REGION_CHUNK_SIZES_LOG2};
    inline constexpr usize conf_num_region_chunk_sizes = array_size(conf_region_chunk_sizes_log2);

    inline constexpr usize conf_min_region_chunk_size_pow2 = conf_region_chunk_sizes_log2[0];
    inline constexpr usize conf_max_region_chunk_size_pow2 = *std::prev(std::end(conf_region_chunk_sizes_log2));

    inline constexpr usize conf_min_region_chunk_size = intpow2(conf_min_region_chunk_size_pow2);
    inline constexpr usize conf_max_region_chunk_size = intpow2(conf_max_region_chunk_size_pow2);

    static_assert(array_unique_ascending(conf_region_chunk_sizes_log2), "sizes must be listed in ascending order"); // TODO : this limitation can be relaxed
    static_assert(all_sizes_valid(conf_region_chunk_sizes_log2), "all sizes must be less than 40");
    static_assert(conf_num_region_chunk_sizes <= conf_num_region_sizes);

    using RegionChunkSizeArray = std::array<uint64, conf_num_region_chunk_sizes>;
    
    inline constexpr RegionChunkSizeArray conf_region_chunk_sizes_array = {CUW3_REGION_CHUNK_SIZES_LOG2};
    
    static_assert(conf_num_region_sizes == conf_num_region_chunk_sizes, "num of regions and num of chunks must be equal.");

    inline constexpr usize conf_region_chunk_pool_contention_split = CUW3_REGION_CHUNK_POOL_CONTENTION_SPLIT;
    inline constexpr usize conf_num_region_chunk_pools = conf_num_region_chunk_sizes;

    static_assert(conf_region_chunk_pool_contention_split > 0, "contention split value must not be zero");

    inline constexpr usize conf_max_contention_split = CUW3_MAX_CONTENTION_SPLIT;

    static_assert(conf_max_contention_split > 0, "contention split value must be greater than zero.");


    inline constexpr usize conf_graveyard_slot_count = CUW3_GRAVEYARD_SLOT_COUNT;


    inline constexpr gsize conf_pool_shard_size_pow2 = CUW3_POOL_SHARD_SIZE_POW2;
    inline constexpr gsize conf_pool_shard_size = intpow2(conf_pool_shard_size_pow2);

    static_assert(conf_pool_shard_size <= conf_min_region_chunk_size, "pool shard size exceedes size of the min size region chunk");

    inline constexpr gsize conf_min_chunk_pow2 = CUW3_MIN_CHUNK_POW2;
    inline constexpr gsize conf_max_chunk_pow2 = CUW3_MAX_CHUNK_POW2;

    static_assert(conf_min_chunk_pow2 <= conf_max_chunk_pow2, "min chunk pow2 is greater that max chunk pow2");
    static_assert(conf_max_chunk_pow2 <= conf_pool_shard_size_pow2, "max chunk pow2 is greater than pool shard pow2");

    inline constexpr gsize conf_num_chunk_pools = conf_max_chunk_pow2 - conf_min_chunk_pow2 + 1;

    inline constexpr gsize conf_max_pow2_chunk_pools = 16; // TODO : def

    static_assert(conf_num_chunk_pools <= conf_max_pow2_chunk_pools, "num of pools exceeeds max capacity");


    // TODO
    inline constexpr gsize conf_fast_arena_min_alignment_pow2 = CUW3_FAST_ARENA_MIN_ALIGNMENT_POW2;
    inline constexpr gsize conf_fast_arena_max_alignment_pow2 = CUW3_FAST_ARENA_MAX_ALIGNMENT_POW2;

    static_assert(conf_fast_arena_min_alignment_pow2 <= conf_fast_arena_max_alignment_pow2, "fast arena min alignment pow2 is greater than fast arena max alignment pow2");
    static_assert(conf_fast_arena_max_alignment_pow2 <= conf_min_region_chunk_size_pow2, "fast arena max alignment is greater than minimal region chunk size");
    
    inline constexpr gsize conf_max_fast_arenas = 8; // TODO : def
    inline constexpr gsize conf_num_fast_arenas = conf_fast_arena_max_alignment_pow2 - conf_fast_arena_min_alignment_pow2 + 1;

    static_assert(conf_num_fast_arenas <= conf_max_fast_arenas, "num of fast arenas exceeds max capacity");
    
    inline constexpr uint64 conf_max_fast_arena_lookup_split = 64;
    inline constexpr uint64 conf_max_fast_arena_lookup_steps = 8; // TODO : proper name
}