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
    inline constexpr uint64 region_chunk_handle_header_data_bits = 12;
    inline constexpr uint64 region_chunk_handle_header_ptr_alignment = 1 << region_chunk_handle_header_data_bits;
    
    using RegionChunkHandleHeaderData = AlignmentPackedPtr<uint64, region_chunk_handle_header_data_bits>;
    
    inline constexpr uint64 region_chunk_handle_min_size = 16; // just to be greater than 8

    // this struct must be at the beginning of each handle
    struct RegionChunkHandleHeader {
        RegionChunkHandleHeaderData data{}; // atomic memory location/readonly memory location
    };
}