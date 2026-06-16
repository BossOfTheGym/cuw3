#pragma once

#include "defs.hpp"
#include "funcs.hpp"

namespace cuw3 {
    // hugepages
    inline constexpr uint64 conf_hugepage_size = CUW3_HUGEPAGE_SIZE;
    static_assert(is_pow2(conf_hugepage_size));

    
    // cacheline & block size
    inline constexpr uint64 conf_cacheline = CUW3_CACHELINE_SIZE;
    static_assert(is_pow2(conf_cacheline));

    inline constexpr uint64 conf_basic_control_block_size = CUW3_CONTROL_BLOCK_SIZE;
    static_assert(is_pow2(conf_basic_control_block_size));

    inline constexpr uint64 conf_control_block_size = align(std::max<uint64>(conf_basic_control_block_size, 2 * conf_cacheline), conf_cacheline);
    inline constexpr uint64 conf_region_handle_size = conf_control_block_size;
    inline constexpr uint64 conf_control_block_size_log2 = intlog2(conf_control_block_size);


    // min alloc & min alignment
    inline constexpr uint64 conf_min_alloc_size = CUW3_MIN_ALLOC_SIZE;
    static_assert(is_pow2(conf_min_alloc_size));
    static_assert(conf_min_alloc_size >= 16);

    inline constexpr uint64 conf_min_alloc_alignment = CUW3_MIN_ALLOC_ALIGNMENT;
    static_assert(is_alignment(conf_min_alloc_alignment));
    static_assert(conf_min_alloc_alignment >= 16);

    inline constexpr uint64 conf_min_alloc_alignment_log2 = intlog2(conf_min_alloc_alignment);


    // region params
    inline constexpr usize conf_max_region_sizes = CUW3_MAX_REGION_SIZES;
    static_assert(conf_max_region_sizes > 0);

    inline constexpr gsize conf_num_regions = conf_max_region_sizes;

    inline constexpr uint64 conf_region_sizes_log2[] = {CUW3_REGION_SIZES_LOG2};
    static_assert(all_sizes_valid(conf_region_sizes_log2));

    inline constexpr usize conf_num_region_sizes = array_size(conf_region_sizes_log2);
    static_assert(conf_num_region_sizes <= conf_max_region_sizes);

    using RegionSizeArray = std::array<uint64, conf_num_region_sizes>;
    
    inline constexpr RegionSizeArray conf_region_sizes_array = {CUW3_REGION_SIZES_LOG2};


    // region chunk params
    inline constexpr uint64 conf_region_chunk_sizes_log2[] = {CUW3_REGION_CHUNK_SIZES_LOG2};
    inline constexpr usize conf_num_region_chunk_sizes = array_size(conf_region_chunk_sizes_log2);

    inline constexpr usize conf_min_region_chunk_size_log2 = conf_region_chunk_sizes_log2[0];
    inline constexpr usize conf_max_region_chunk_size_log2 = *std::prev(std::end(conf_region_chunk_sizes_log2));

    inline constexpr usize conf_min_region_chunk_size = intpow2(conf_min_region_chunk_size_log2);
    inline constexpr usize conf_max_region_chunk_size = intpow2(conf_max_region_chunk_size_log2);

    static_assert(array_unique_ascending(conf_region_chunk_sizes_log2), "sizes must be listed in ascending order");
    static_assert(all_sizes_valid(conf_region_chunk_sizes_log2));
    static_assert(conf_num_region_chunk_sizes <= conf_num_region_sizes);

    using RegionChunkSizeArray = std::array<uint64, conf_num_region_chunk_sizes>;
    
    inline constexpr RegionChunkSizeArray conf_region_chunk_sizes_array = {CUW3_REGION_CHUNK_SIZES_LOG2};
    static_assert(conf_num_region_sizes == conf_num_region_chunk_sizes, "num of regions and num of chunks must be equal.");

    inline constexpr usize conf_max_cached_chunk_size_id = CUW3_MAX_CACHED_CHUNK_SIZE_ID;
    static_assert(conf_max_cached_chunk_size_id < conf_num_region_chunk_sizes);
    inline constexpr usize conf_max_cached_chunk_size = intpow2(conf_region_chunk_sizes_array[conf_max_cached_chunk_size_id]); 

    
    // region pool params
    inline constexpr usize conf_max_contention_split = CUW3_MAX_CONTENTION_SPLIT;
    static_assert(conf_max_contention_split <= 64 && conf_max_contention_split > 0, "contention split value must be greater than zero.");


    // thread graveyard params
    inline constexpr usize conf_graveyard_slot_count = CUW3_GRAVEYARD_SLOT_COUNT;


    // fast arena allocator params
    inline constexpr gsize conf_min_alignment_log2 = CUW3_MIN_ALIGNMENT_LOG2;
    inline constexpr gsize conf_max_alignment_log2 = CUW3_MAX_ALIGNMENT_LOG2;

    static_assert(conf_min_alignment_log2 >= conf_min_alloc_alignment_log2);
    static_assert(conf_min_alignment_log2 <= conf_max_alignment_log2);
    static_assert(conf_max_alignment_log2 <= 8192);

    inline constexpr gsize conf_fast_arena_min_alignment_log2 = conf_min_alignment_log2;
    inline constexpr gsize conf_fast_arena_max_alignment_log2 = conf_max_alignment_log2;

    static_assert(conf_fast_arena_max_alignment_log2 <= conf_min_region_chunk_size_log2, "fast arena max alignment is greater than minimal region chunk size");
    
    inline constexpr gsize conf_num_fast_arenas = conf_fast_arena_max_alignment_log2 - conf_fast_arena_min_alignment_log2 + 1;
    inline constexpr gsize conf_max_fast_arenas = conf_num_fast_arenas;

    inline constexpr uint64 conf_max_fast_arena_lookup_split = CUW3_MAX_FAST_ARENA_LOOKUP_SPLIT;
    static_assert(conf_max_fast_arena_lookup_split > 0);
    static_assert(conf_max_fast_arena_lookup_split <= 256);

    inline constexpr uint64 conf_max_fast_arena_lookup_steps = CUW3_MAX_FAST_ARENA_LOOKUP_STEPS;
    static_assert(conf_max_fast_arena_lookup_steps > 0);
    static_assert(conf_max_fast_arena_lookup_steps <= 24);

    inline constexpr uint64 conf_size_cutoff = CUW3_SIZE_CUTOFF;
    static_assert(conf_size_cutoff > 0);
}