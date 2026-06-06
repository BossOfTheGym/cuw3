INTRO

This is cuw3, a lock-free (if we dont consider vmem committing) arena-based memory alloctor.

This is mostly a personal learning R&D project.

I targeted at creation, first of all, a lock-free allocator. In the process I also created (implemented to be fair as some ideas were not completely new) several data structures.

I wanted to try something new. So there is no strict code style. No obligatory sticking to known idioms. No rights or wrongs. Just pure experiment. 


DESIGN DECISIONS

There were several crucial design decisions that affected the final outlook of an allocator.

1. No allocation metadata stored next to the allocation itself. Metadata is stored completely separately. But allocated memory can be used to place whatever data necessary to help deallocation process after it was, well, deallocated. Like, you can store pointer to the next free chunk and so on. If you store metadata next to the managed memory chunk or allocation itself it produces way too much mess.

2. You may use whatever allocator you want. Allocator system provides facilities so you can use your custom allocator. Allocator system gives you memory chunk handle as well as memory chunk memory for your needs. Allocator system has unified way of determining what chunk pointer belongs.

3. This allocator implementation violates standard malloc/free function convention. Free explicitly requries you to pass size besides the freed memory. In this way, it resembles common virtual memory allocation functions. It simplifies internal logic greately.

4. Allocator has maximum allocation size. Even though it can be done using vmem allocations. My bad probably, but the main goal was to create and test the allocator infrastructure itself. This particular task is not a big problem to implement, just another check in the code.

5. Thread-local allocators do not attempt to cache any of the memory chunks. Just because. It would be good to have though. But to keep things simple there is none implemented. Also allocator does not attempt to cache commit status of the memory - allocator could have postpone decommitting of the vmem to decrease amount of waiting. 

6. By default allocator uses arena-based allocators. One allocator serves relatively small memory requests. The other serves mixed-sized memory requests (up to Mib's).

7. Thread graveyard is not that necessary. If there are any other threads holding memory from current thread they may try to acquire role of cleaning thread after current thread has exited. The problem here is to release the role of cleaning thread. We can either move the dead thread allocatot into some other place we can retrieve it from later (graveyard) or too continuosly try to release role of the cleaning thread freeing any accumulated memory in the process with no way to stop.


IMPLEMENTATION NOTES

Allocator basically consists of several distinct components:
* region_chunk_allocator - allocates memory chunks and chunk handles later consumed by some allocator subsystem.
* fast_arena_small_allocator - named so because initially was designed to serve only small-sized allocations but can fulfil, in fact, any request that currently fits into the memory chunk. Maintains list of free arenas in a simple list.
* fast_arena_step_split_allocator - pretty much acts like fast_arena_small_allocator but organizes free arenas according to their free size more granularly.
* retire-reclaim scheme - not a distinct component in the common sense but rather an algorithm that helps to retire hierarchical resources.
* thread graveyard - data structure that is used to retire dead threads (their thread-local allocators) when there is some memory to free.
* thread-local allocator - placeholder for all memory allocators
* allocator - placeholder for region_chunk_allocator, serves memory allocation/deallocation requests.


REGION CHUNK ALLOCATOR

Heart of the allocator. Manages chunks and their handles. Chunks are categorised by their size. All chunks of the same size comprise a region. All regions are stored as one vmem allocation. All chunk handles of all regions are stored in a separate vmem allocation.  Utilizes atomic lock-free pools to allocate free chunks. Has limited amount of chunks - limited allocation capacity as a conclusion. Allocated chunks and their handles are then consumed by higher-level allocators.


GENERAL PURPOSE ARENA ALLOCATORS

All allocators in cuw3 use arena allocation algorithm underneath. It differs from the standard arena allocation algorithm in a way that it allows memory recycling. That is, you don't have to throw away whole arena to deallocate stuff and you can track the moment when you can safely reset the arena. This is done by additionally tracking size of deallocated memory and if it matches the top (current point from where new allocations start) you can safely reset the arena.

Also (yet not implemented) it is possible to allocate aligned memory from any arena: memory that goes wasted due to top alignment goes into the freed counter. We waste some memory but get general solution.

Arenas can be of two types: slow and fast, at least how I call them. Both of them work almost similarly: both of them allocate from the monotonically increasing top, both of them track the moment they can be resetted. Both increase freed counter when memory is deallocated. Slow arena gives you ability to track earlier made allocations (it basically stores a list of them). It does it by saving each allocation into the list (array). Top increases monotonically - ptrs are sorted we can apply bsearch to search if allocation exists and obtain its size.

I did not want to implement slow arena because:
1. It will definitely be slower. You would have to bsearch allocation in the array each time you want deallocate something.
2. Why if it will be slower?

But slow arena gives you ability to track memory allocations (we can catch double-free situation which we cannot do with fast arena). Could have been used in debugging.

Fast arena is fast because it stores nothing. It just increases the freed counter. So 'free' is basically an increment.


FAST ARENA SMALL ALLOCATOR

Arena-based allocator. It was named this way because initially was intended for small-size allocations only. But the data structure itself allows you allocate practically any size that can fit into the arena. Categorises arenas by alignment: there are several lists, arena from such a list can allocate memory using only one specified alignment. Works pretty fast. All free arenas of the same category are contained in the same list. 


FAST ARENA STEP-SPLIT ALLOCATOR

Kind of similar to fast arena small allocator but arenas additionally categorised by the size remaining. Size space is split into steps. Each next step is twice as large as the previous one. Each step is split into 'split' parts. So, firstly, allocator locates what step arena belongs to and then locates the step. More detailed description can be found in 'fast_arena_step_split_allocator.hpp'


RETIRE-RECLAIM SCHEME

Not a distinct data structure but rather an algorithm that allows you to safely retire some resource from the other thread. More info can be found in 'retire_reclaim.hpp'. In short: when some thread attempts to retire resource it always succeeds but may become responsible to retire the parent resource as well.


THREAD GRAVEYARD

Since I wanted to let allocator to have a natural limit on how much allcoations it can reclaim at once (from other threads) I added this data structure (but never actually implemented this). This is a fixed-size slotted storage. Each slot can be acquired exclusively. Any item that cannot find itself a free slot goes into the common list. Items from commonn list are sometimes distributed between free slots. See 'thread_graveyard.hpp' for details.


THREAD-LOCAL ALLOCATOR

Just a container for all of the beforementioned allocators: fast arena small allocator and fast arena step split allocator. Any allocators you want to use you put here. See 'thread_local_allocator.hpp' for reference but there is not that much in it.


BENCHMARKS

Allocator outperforms standard allocator in the small-size case. Even without switching off checks and assretions. And in pure middle-size case. But loses in mixed-size case. Not a big surprise. I guess, It happens due to step-split allocator (TODO: perf analyze). Currently, it mostly suck in the mt case :(


CONCLUSION

The most simple and clean approach would be to use just small arena allocator: the most general solution even omit the alignment categorization. Good on waords but may potentionally lead to increased wasted amount of memory. But that's a tradeoff in favor of speed. But!!! nobody prevents you to add additional categorization to the allocator: you categorize allocations by tags, labels, lifetime duration. And it all will be easy: just another allocator slot in the thead local allocator.

Chunk caching is a must have.