#pragma once

#include "defs.hpp"
#include "funcs.hpp"

namespace cuw3 {
    inline constexpr uint64 conf_cacheline = CUW3_CACHELINE_SIZE;
    inline constexpr uint64 conf_region_handle_size = 2 * conf_cacheline;
    inline constexpr uint64 conf_control_block_size = CUW3_CONTROL_BLOCK_SIZE;

    inline constexpr usize conf_max_region_sizes = CUW3_MAX_REGION_SIZES;

    inline constexpr uint64 conf_region_sizes_log2[] = {CUW3_REGION_SIZES_LOG2};
    inline constexpr usize conf_num_region_sizes = array_size(conf_region_sizes_log2);
    
    static_assert(all_sizes_valid(conf_region_sizes_log2), "all region sizes must be less than 40");
    static_assert(conf_num_region_sizes <= conf_max_region_sizes);

    using RegionSizeArray = std::array<uint64, conf_num_region_sizes>;
    
    inline constexpr RegionSizeArray conf_region_sizes_array = {CUW3_REGION_SIZES_LOG2};

    inline constexpr uint64 conf_region_chunk_sizes_log2[] = {CUW3_REGION_CHUNK_SIZES_LOG2};
    inline constexpr usize conf_num_region_chunk_sizes = array_size(conf_region_chunk_sizes_log2);

    static_assert(array_unique_ascending(conf_region_chunk_sizes_log2), "sizes must be listed in ascending order");
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
}