#pragma once

#include "ptr.hpp"
#include "conf.hpp"

namespace cuw3 {
    // *** implementation forethought ***
    // data bits representation changes:
    // * when chunk is not allocated then data bits are not used, pointer bits store next pointer
    // * when chunk is allocated we can effeectively store some pointer
    //   * how convinient, we can store thread local pointer in ptr section here and allocator type used for the chunk in the data section
    /*

    https://en.cppreference.com/w/cpp/language/multithread.html

    Data races

    Different threads of execution are always allowed to access (read and modify) different memory locations concurrently, with no interference and no synchronization requirements.

    Two expression evaluations conflict if one of them modifies a memory location or starts/ends the lifetime of an object in a memory location, and the other one reads or modifies the same memory location or starts/ends the lifetime of an object occupying storage that overlaps with the memory location.

    A program that has two conflicting evaluations has a data race unless

    both evaluations execute on the same thread or in the same signal handler, or
    both conflicting evaluations are atomic operations (see std::atomic), or
    one of the conflicting evaluations happens-before another (see std::memory_order). 

    If a data race occurs, the behavior of the program is undefined. 

    */
    // *** multithreading notes ***
    // 1. all accesses to this field must be atomic or else it will be UB (almost all)
    // 2. all accesses to this field from list modification context MUST be atomic (or else it's UB)
    // 3. after we have allocated chunk (it means we either acquired it from the free_stack or from the free_list)
    //    we can atomically modify state of header data field (there can still be threads attempting to read its value though it is totally fine if they read some garbage)
    // 4. after that (if we dont need any modification done to this field) we can read this value non-atomically
    // 5. whenever we need to modify its state we do this, again, atomically like we did in 3.
    // 6. => when we want to return chunk back to list we gotta resort to atomic modifications
    //
    // This must comply with Data races definition from C++ (no data races here).
    //
    // Yeah, setting fixed amount of bits imposes alignment constraints on thread local storage.
    // But it must not be an issue because it will be allocated via virtual memory facility anyway (and common page size is 4Kib anyway (12 zero bits))
    //
    // As the name suggests: this must be placed at the beginning of the handle memory location
    using RegionChunkHandleHeaderDataRaw = uint64;
    
    inline constexpr uint64 region_chunk_handle_header_data_bits = 12;
    inline constexpr uint64 region_chunk_handle_header_ptr_alignment = 1 << region_chunk_handle_header_data_bits;
    
    using RegionChunkHandleHeaderData = AlignmentPackedPtr<uint64, region_chunk_handle_header_data_bits>;
    
    inline constexpr uint64 region_chunk_handle_min_size = 16; // just to be greater than 8

    using RegionChunkPoolLinkType = uint32;

    // this struct must be at the beginning of each handle
    struct RegionChunkHandleHeader {
        RegionChunkHandleHeaderData data{}; // atomic memory location/readonly memory location
    };

    // memory order can be relaxed here: no memory operation should depend on modification of distinct chunk handle
    // (we do stricter memory accesses on shared data structures (list) anyway)
    struct RegionChunkHandleHeaderView {
        // chunk has already been allocated here
        // starts new chunk lifetime immediately after it was allocated
        // first you fetch chunk from the list, then you start its lifetime
        // basically it means that you start using this chunk and initialize the data structure that will be placed at its location
        // thread exclusively acquires the chunk, no other thread can modify chunk header state after handle has been acquired
        void start_chunk_lifetime(void* owner, uint64 chunk_type) {
            auto new_data = RegionChunkHandleHeaderData::packed(owner, chunk_type);
            std::atomic_ref{header->data}.store(new_data, std::memory_order_relaxed);
        }

        // chunk has already been allocated here
        // thread own chunk handle exclusively, other thread can only read this memory location (even atomically) - no race here
        void* get_owner() {
            return header->data.ptr();
        }

        // chunk has already been allocated here
        // thread own chunk handle exclusively, other thread can only read this memory location (even atomically) - no race here
        uint64 get_type() {
            return header->data.data();
        }


        // chunk has already been allocated here
        // we want to return chunk back into the shared data structure (list)
        // now modification can produce a data race, so we have to make an atomic modification
        void set_next_chunk(RegionChunkPoolLinkType next) {
            auto new_data = RegionChunkHandleHeaderData::packed_shifted(next, 0);
            std::atomic_ref{header->data}.store(new_data, std::memory_order_relaxed);
        }

        // chunk may have become allocated here (either still in the list or already allocated)
        // we assume that chunk is still in the shared list
        // we do want to allocate it so we also assume concurrent access
        // that's why we do n atomic access here
        RegionChunkPoolLinkType get_next_chunk() {
            return std::atomic_ref{header->data}.load(std::memory_order_relaxed).value_shifted();
        }


        RegionChunkHandleHeader* header{};
    };

    // region chunk header always goes first
    // this function zero initializes wohle handle without touching handle itself as (even though not crictical) introduces a race
    // race can be considered 'safe' as there will be only threads possibly reading the value
    template<class T>
    T* initz_region_chunk_handle(void* chunk_handle, gsize chunk_handle_size) {
        CUW3_ASSERT(chunk_handle_size >= conf_region_handle_size, "too little space for a chunk handle");
        std::memset(advance_ptr(chunk_handle, sizeof(RegionChunkHandleHeader)), 0x00, chunk_handle_size - sizeof(RegionChunkHandleHeader));
        return (T*)chunk_handle;
    }

    enum class RegionChunkType : uint32 {
        PoolShardPool,
        FastArena,
    };

    // dummy type to store both handle + memory when chunk is retired 
    struct RegionChunkHandle {
        RegionChunkHandleHeader header{};
        RegionChunkHandle* next{};
        void* chunk_memory{};
        uint64 chunk_size{};
    };

    static_assert(sizeof(RegionChunkHandle) <= conf_region_handle_size, "too big region chunk handle");
}