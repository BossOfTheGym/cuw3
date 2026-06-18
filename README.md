# cuw3

## Intro

This is **cuw3**, a lock-free (if we don't consider vmem committing) arena-based memory allocator.

This is mostly a personal learning R&D project.

I targeted, first of all, the creation of a lock-free allocator. In the process I also created (implemented, to be fair, as some ideas were not completely new) several data structures.

I wanted to try something new. So there is no strict code style. No obligatory sticking to known idioms. No rights or wrongs. Just pure experiment.

## Design Decisions (and Notes)

There were several crucial design decisions that affected the final outlook of the allocator.

1. **No allocation metadata stored next to the allocation itself.** Metadata is stored completely separately. But allocated memory can be used to place whatever data is necessary to help the deallocation process after it was, well, deallocated. Like, you can store a pointer to the next free chunk and so on. If you store metadata next to the managed memory chunk or allocation itself, it produces way too much mess.

2. **You may use whatever allocator you want.** The allocator system provides facilities so you can use your custom allocator. The allocator system gives you a memory chunk handle as well as memory chunk memory for your needs. The allocator system has a unified way of determining which chunk a pointer belongs to.

3. **The allocator guarantees that you can allocate by whatever function you want** (cuw3 API, of course) but free using one common free function. All allocating algorithms must support this behaviour.

4. **This allocator implementation violates the standard malloc/free function convention.** Free explicitly requires you to pass the size besides the freed memory. In this way, it resembles common virtual memory allocation functions. It simplifies internal logic greatly. It also gives you the opportunity to extend the allocator API in whatever way you want. Like, add allocators with tags or specifically allocate using whatever algorithm is implemented: pool, arena, or anything else. You can have way more control over your allocations. You know the usage pattern, you know the proper allocator, you choose it. You know the access pattern, you know how long your allocations will live — you choose the proper function.

5. **The allocator has a maximum allocation size.** Even though it can be done using vmem allocations. My bad, probably, but the main goal was to create and test the allocator infrastructure itself. This particular task is not a big problem to implement, just another check in the code.

6. **Thread-local allocators do cache chunks.**

7. **By default the allocator uses arena-based allocators.** One allocator serves relatively small memory requests. The other serves mixed-sized memory requests (up to MiB's).

8. **The thread graveyard is not that necessary.** If there are any other threads holding memory from the current thread, they may try to acquire the role of cleaning thread after the current thread has exited. The problem here is to release the role of cleaning thread. We can either move the dead thread allocator into some other place we can retrieve it from later (graveyard), or continuously try to release the role of the cleaning thread, freeing any accumulated memory in the process with no way to stop.

## Implementation Notes

The allocator basically consists of several distinct components:

- **`region_chunk_allocator`** — allocates memory chunks and chunk handles later consumed by some allocator subsystem.
- **`fast_arena_small_allocator`** — named so because initially it was designed to serve only small-sized allocations, but can fulfil, in fact, any request that currently fits into the memory chunk. Maintains a list of free arenas in a simple list.
- **`fast_arena_step_split_allocator`** — pretty much acts like `fast_arena_small_allocator` but organizes free arenas according to their free size more granularly.
- **retire-reclaim scheme** — not a distinct component in the common sense, but rather an algorithm that helps to retire hierarchical resources.
- **thread graveyard** — a data structure that is used to retire dead threads (their thread-local allocators) when there is some memory to free.
- **thread-local allocator** — placeholder for all memory allocators.
- **allocator** — placeholder for `region_chunk_allocator`; serves memory allocation/deallocation requests.

## Region Chunk Allocator

The heart of the allocator. Manages chunks and their handles. Chunks are categorised by their size. All chunks of the same size comprise a region. All regions are stored as one vmem allocation. All chunk handles of all regions are stored in a separate vmem allocation. Utilizes atomic lock-free pools to allocate free chunks. Has a limited amount of chunks — limited allocation capacity as a consequence. Allocated chunks and their handles are then consumed by higher-level allocators.

## General Purpose Arena Allocators

All allocators in cuw3 use the arena allocation algorithm underneath. It differs from the standard arena allocation algorithm in that it allows memory recycling. That is, you don't have to throw away the whole arena to deallocate stuff, and you can track the moment when you can safely reset the arena. This is done by additionally tracking the size of deallocated memory, and if it matches the top (the current point from where new allocations start) you can safely reset the arena.

Also (yet not implemented) it is possible to allocate aligned memory from any arena: memory that goes wasted due to top alignment goes into the freed counter. We waste some memory but get a general solution.

Arenas can be of two types: slow and fast, at least how I call them. Both of them work almost similarly: both allocate from the monotonically increasing top, and both track the moment they can be reset. Both increase the freed counter when memory is deallocated. The slow arena gives you the ability to track earlier made allocations (it basically stores a list of them). It does so by saving each allocation into the list (array). The top increases monotonically — pointers are sorted, so we can apply binary search to check if an allocation exists and obtain its size.

I did not want to implement the slow arena because:

1. It will definitely be slower. You would have to binary-search the allocation in the array each time you want to deallocate something.
2. Why, if it will be slower?

But the slow arena gives you the ability to track memory allocations (we can catch the double-free situation, which we cannot do with the fast arena). It could have been used in debugging.

The fast arena is fast because it stores nothing. It just increases the freed counter. So 'free' is basically an increment.

## Fast Arena Small Allocator

An arena-based allocator. It was named this way because initially it was intended for small-size allocations only. But the data structure itself allows you to allocate practically any size that can fit into the arena. Categorises arenas by alignment: there are several lists, and an arena from such a list can allocate memory using only one specified alignment. Works pretty fast. All free arenas of the same category are contained in the same list.

A fast arena gives you the ability to allocate one big range and deallocate parts of it. However, this must be explicitly supported, so the current implementation must be checked. Alignment must also be considered.

## Fast Arena Step-Split Allocator

Kind of similar to the fast arena small allocator, but arenas are additionally categorised by the size remaining. The size space is split into steps. Each next step is twice as large as the previous one. Each step is split into 'split' parts. So, firstly, the allocator locates what step the arena belongs to and then locates the step. A more detailed description can be found in `fast_arena_step_split_allocator.hpp`.

## Retire-Reclaim Scheme

Not a distinct data structure but rather an algorithm that allows you to safely retire some resource from another thread. More info can be found in `retire_reclaim.hpp`. In short: when some thread attempts to retire a resource it always succeeds, but may become responsible for retiring the parent resource as well.

## Thread Graveyard

Since I wanted to let the allocator have a natural limit on how much it can reclaim at once (from other threads), I added this data structure (but never actually implemented this). This is a fixed-size slotted storage. Each slot can be acquired exclusively. Any item that cannot find itself a free slot goes into the common list. Items from the common list are sometimes distributed between free slots. See `thread_graveyard.hpp` for details.

## Thread-Local Allocator

Just a container for all of the aforementioned allocators: the fast arena small allocator and the fast arena step-split allocator. Any allocators you want to use, you put here. See `thread_local_allocator.hpp` for reference, but there is not that much in it.

## Benchmarks

Now it has some potential. Basic chunk caching make it outperform std allocator. Warning: there was no cross-thread deallocation tests.

## Conclusion

The most simple and clean approach would be to use just the small arena allocator: the most general solution, even omitting the alignment categorization. Good on words, but may potentially lead to an increased amount of wasted memory. But that's a tradeoff in favor of speed. But!!! Nobody prevents you from adding additional categorization to the allocator: you categorize allocations by tags, labels, lifetime duration. And it will all be easy: just another allocator slot in the thread-local allocator.

## TODOs & Notes

- **Allocator / Thread-Local Allocator / cuw3 code must be reorganised.** At least this code. It must be rewritten in a more procedural style. The TLA depends on the region chunk allocator and the thread graveyard — components of the global context. Most of the functions from `allocator.hpp` are, in fact, functions of the TLA.

- **Chunk caching** Better chunk caching would be great. Now it stupidly caches one chunk of each size at max. By default it leads up to 2 + 4 + 8 + 16 + 32 + 64 = 126 Mib of cached memory. Pretty much per thread. Chunk caching can be made at least shared: bigger chance to reuse memory rather then pile it up somewhere without any use.

Hope it will be useful to anybody some day. That's it folks!
