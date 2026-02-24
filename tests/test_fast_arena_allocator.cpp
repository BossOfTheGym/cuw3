#include "cuw3/assert.hpp"
#include "cuw3/conf.hpp"
#include "cuw3/vmem.hpp"
#include "cuw3/funcs.hpp"
#include "cuw3/fast_arena_allocator.hpp"

#include <memory>
#include <random>
#include <variant>
#include <algorithm>

using namespace cuw3;

namespace fast_arena_tests {
    struct alignas(region_chunk_handle_header_ptr_alignment) Owner {
    } dummy_owner;

    struct FastArenaAllocation {
        explicit operator bool() const {
            return memory;
        }

        void* memory{};
        uint64 size{};
    };

    struct FastArenaUnit {
        struct VMemDeleter {
            void operator()(void* memory) const {
                vmem_free(memory, size);
            }

            uint64 size{};
        };

        using VMemPtr = std::unique_ptr<void, VMemDeleter>;


        static uint64 adjust_alignment(uint alignment) {
            return std::max<uint>(alignment, conf_min_alloc_alignment);
        }

        static uint64 adjust_memory_size(uint alignment, uint memory_size) {
            return align(memory_size, alignment);
        }

        static VMemPtr create_vmem_ptr(uint alignment, uint memory_size) {
            CUW3_CHECK(is_alignment(alignment), "not an alignment");

            alignment = adjust_alignment(alignment);
            memory_size = adjust_memory_size(alignment, memory_size);

            void* arena_memory = vmem_alloc(memory_size, VMemAllocType::VMemReserveCommit);
            CUW3_CHECK(arena_memory, "failed to allocate memory");

            return {arena_memory, {memory_size}};
        }


        FastArenaUnit(uint alignment, uint memory_size) : vmem_ptr{create_vmem_ptr(alignment, memory_size)} {
            FastArenaConfig config = {
                .owner = &dummy_owner,

                .arena_handle = &arena,
                .arena_memory = vmem_ptr.get(),

                .arena_handle_size = conf_control_block_size,
                .arena_alignment = alignment,
                .arena_memory_size = vmem_ptr.get_deleter().size,
            };
            CUW3_CHECK(FastArenaView::create_fast_arena(config), "Failed to create arena");
        }

        [[nodiscard]] FastArenaAllocation allocate(uint64 size) {
            return {FastArenaView{&arena}.acquire(size), size};
        }

        void deallocate(FastArenaAllocation allocation) {
            auto view = FastArenaView{&arena};
            view.release(allocation.memory, allocation.size);
            if (view.resettable()) {
                view.reset();
            }
        }

        uint64 remaining() const {
            return FastArenaView{const_cast<FastArena*>(&arena)}.remaining(); // ugly
        }

        uint64 alignment() const {
            return FastArenaView{const_cast<FastArena*>(&arena)}.alignment(); // ugly
        }

        bool empty() const {
            return FastArenaView{const_cast<FastArena*>(&arena)}.empty(); // ugly
        }

        FastArena arena{};
        VMemPtr vmem_ptr;
    };

    void test_arena_full_exaustion(uint alignment, uint memory_size) {
        FastArenaUnit arena{alignment, memory_size};

        std::vector<FastArenaAllocation> allocations{};
        while (arena.remaining() >= arena.alignment()) {
            auto allocation_size = arena.alignment();
            auto allocation = arena.allocate(allocation_size);
            CUW3_CHECK(allocation, "failed to allocate when expected");
            CUW3_CHECK(allocation.size == allocation_size, "unexpected alignment size");

            allocations.push_back(allocation);
        }
        for (uint i = 1; i < allocations.size(); i++) {
            auto& prev = allocations[i - 1];
            auto& curr = allocations[i];
            CUW3_CHECK((uintptr)prev.memory + prev.size == (uintptr)curr.memory, "unexpected allocations' properties");
        }
        for (auto& allocation : allocations) {
            arena.deallocate(allocation);
        }
        CUW3_CHECK(arena.empty(), "arena was expected to be empty");
    }



    struct FastArenaTestAllocation {
        explicit operator bool() const {
            return id;
        }

        uint id{};
        FastArenaAllocation allocation{};
    };

    struct FastArenaTestContext {
        static constexpr uint null_id = 0xffffffff;

        void push(FastArenaAllocation allocation) {
            uint id = curr_id++;
            if (id == null_id) {
                id = curr_id++;
            }
            allocations.push_back({id, allocation});
        }

        [[nodiscard]] FastArenaTestAllocation pop() {
            CUW3_CHECK(!allocations.empty(), "no allocations to pop");

            auto allocation = allocations.back();
            allocations.pop_back();
            return allocation;
        }

        [[nodiscard]] FastArenaTestAllocation pop(uint id) {
            auto it = std::find_if(allocations.begin(), allocations.end(), [&] (auto& alloc) {
                return alloc.id == id;
            });
            if (it == allocations.end()) {
                return {};
            }
            auto index = it - allocations.begin();
            std::swap(allocations[index], allocations.back());
            return pop();
        }

        bool empty() const {
            return allocations.empty();
        }

        void reset() {
            CUW3_CHECK(empty(), "attempt to reset non-empty context");
            curr_id = 0;
        }

        std::vector<FastArenaTestAllocation> allocations{};
        uint curr_id{};
    };

    struct FastArenaAllocate {
        void exec(FastArenaUnit& arena, FastArenaTestContext& context) const {
            auto allocation = arena.allocate(size);
            CUW3_CHECK(allocation, "allocation was not expected to be empty");

            context.push(allocation);
        }

        uint64 size{};
    };

    struct FastArenaDeallocateId {
        void exec(FastArenaUnit& arena, FastArenaTestContext& context) const {
            auto entry = context.pop(id);
            arena.deallocate(entry.allocation);
        }

        uint id{};
    };

    struct FastArenaDeallocatePop {
        void exec(FastArenaUnit& arena, FastArenaTestContext& context) const {
            auto entry = context.pop();
            arena.deallocate(entry.allocation);
        }
    };

    struct FastArenaDeallocateAll {
        void exec(FastArenaUnit& arena, FastArenaTestContext& context) const {
            while (!context.empty()) {
                arena.deallocate(context.pop().allocation);
            }
        }
    };

    struct FastArenaResetContext {
        void exec(FastArenaUnit& arena, FastArenaTestContext& context) const {
            context.reset();
        }
    };

    struct FastArenaCheckEmpty {
        void exec(FastArenaUnit& arena, FastArenaTestContext& context) const {
            CUW3_CHECK(arena.empty(), "arena was not empty");
        }
    };

    struct FastArenaCheckNonEmpty {
        void exec(FastArenaUnit& arena, FastArenaTestContext& context) const {
            CUW3_CHECK(!arena.empty(), "arena was empty");
        }
    };

    using FastArenaTestCommand = std::variant<
        FastArenaAllocate,
        FastArenaDeallocateId,
        FastArenaDeallocatePop,
        FastArenaDeallocateAll,
        FastArenaResetContext,
        FastArenaCheckEmpty,
        FastArenaCheckNonEmpty
    >;

    struct FastArenaTestCommandList {
        void allocate(uint64 size) {
            commands.push_back(FastArenaAllocate{size});
        }

        void deallocate_id(uint id) {
            commands.push_back(FastArenaDeallocateId{id});
        }

        void deallocate_pop() {
            commands.push_back(FastArenaDeallocatePop{});
        }

        void deallocate_all() {
            commands.push_back(FastArenaDeallocateAll{});
        }

        void reset_context() {
            commands.push_back(FastArenaResetContext{});
        }

        void check_empty() {
            commands.push_back(FastArenaCheckEmpty{});
        }

        void check_non_empty() {
            commands.push_back(FastArenaCheckNonEmpty{});
        }


        void execute(FastArenaUnit& arena) {
            FastArenaTestContext context{};
            auto visitor = [&] (auto& command) {
                command.exec(arena, context);
            };
            for (auto& command : commands) {
                std::visit(visitor, command);
            }
        }


        std::vector<FastArenaTestCommand> commands{};
    };


    void test_arena_partial_exaustion1(uint desired_alignment) {
        uint alignment = FastArenaUnit::adjust_alignment(desired_alignment);
        uint memory_size = FastArenaUnit::adjust_memory_size(alignment, 2 * (1 + 2 + 3 + 4) * alignment);
        FastArenaUnit arena{alignment, memory_size};

        FastArenaTestCommandList commands;
        commands.allocate(1 * alignment);
        commands.allocate(2 * alignment);
        commands.allocate(3 * alignment);
        commands.allocate(4 * alignment);
        commands.deallocate_all();
        commands.check_empty();
        commands.reset_context();
        
        commands.execute(arena);
    }

    void test_arena_partial_exaustion2(uint desired_alignment) {
        uint alignment = FastArenaUnit::adjust_alignment(desired_alignment);
        uint memory_size = FastArenaUnit::adjust_memory_size(alignment, (1 + 1 + 2 + 3 + 4) * alignment);
        FastArenaUnit arena(alignment, memory_size);

        FastArenaTestCommandList commands{};
        commands.allocate(1 * alignment);
        commands.allocate(1 * alignment);
        commands.allocate(2 * alignment);
        commands.allocate(3 * alignment);
        commands.allocate(4 * alignment);
        commands.deallocate_all();
        commands.check_empty();
        commands.reset_context();

        commands.allocate(4 * alignment);
        commands.allocate(1 * alignment);
        commands.deallocate_id(0);
        commands.deallocate_id(1);
        commands.check_empty();
        commands.reset_context();

        commands.allocate(2 * alignment);
        commands.allocate(1 * alignment);
        commands.allocate(3 * alignment);
        commands.deallocate_id(1);
        commands.deallocate_id(0);
        commands.deallocate_id(2);
        commands.check_empty();
        commands.reset_context();

        commands.allocate(1 * alignment);
        commands.allocate(2 * alignment);
        commands.allocate(1 * alignment);
        commands.deallocate_id(2);
        commands.deallocate_id(0);
        commands.deallocate_id(1);
        commands.check_empty();
        commands.reset_context();

        commands.allocate(3 * alignment);
        commands.allocate(4 * alignment);
        commands.deallocate_id(1);
        commands.check_non_empty();
        commands.deallocate_id(0);
        commands.check_empty();
        commands.reset_context();

        commands.allocate(1 * alignment); // 0
        commands.allocate(1 * alignment); // 1
        commands.allocate(2 * alignment); // 2
        commands.allocate(3 * alignment); // 3
        commands.check_non_empty();
        commands.deallocate_id(1);
        commands.deallocate_id(3);
        commands.check_non_empty();
        commands.allocate(4 * alignment); // 4
        commands.deallocate_id(0);
        commands.check_non_empty();
        commands.deallocate_id(2);
        commands.check_non_empty();
        commands.deallocate_id(4);
        commands.check_empty();
        commands.reset_context();

        commands.execute(arena);
    }
}

namespace fast_arena_allocator_tests {
    // TODO : owner
    // TODO : storage for memory & handles
    // TODO : allocator: fast arena allocator + storage
    struct VMemDeleter {
        void operator()(void* ptr) const {
            vmem_free(ptr, size);
        }

        uint64 size{};
    };

    using VMemPtrBase = std::unique_ptr<void, VMemDeleter>;

    struct VMemPtr : VMemPtrBase {
        using VMemPtrBase::VMemPtrBase;

        static VMemPtr create(uint64 size) {
            size = align(size, vmem_page_size());
            void* ptr = vmem_alloc(size, VMemAllocType::VMemReserveCommit);
            if (!ptr) {
                return {};
            }
            return {ptr, VMemDeleter{size}};
        }

        void* ptr() const {
            return get();
        }

        uint64 size() const {
            return get_deleter().size;
        }
    };

    struct TestRawFastArenaData {
        explicit operator bool() const {
            return arena;
        }

        void* arena{};
        void* handle{};
    };

    struct TestFastArenaMemoryStorage {
        TestFastArenaMemoryStorage(uint _num_arenas, uint _arena_size) {
            num_arenas = _num_arenas;
            arena_size = align(_arena_size, vmem_page_size());

            uint64 arena_data_size = num_arenas * arena_size;
            arena_data_ptr = VMemPtr::create(arena_data_size);
            CUW3_CHECK(arena_data_ptr, "failed to create arena_data_ptr");

            uint64 arena_handles_size = num_arenas * sizeof(FastArena);
            arena_handles_ptr = VMemPtr::create(arena_handles_size);
            CUW3_CHECK(arena_handles_ptr, "failed to create arena_handles_ptr");
        }
        
        [[nodiscard]] TestRawFastArenaData _get_arena_by_id(uint arena_id) {
            void* arena_handle = advance_arr(arena_handles_ptr.get(), sizeof(FastArena), arena_id);
            void* arena_data = advance_arr(arena_data_ptr.get(), arena_size, arena_id);
            return {arena_data, arena_handle};
        }

        bool _check_arena(FastArena* arena) {
            if (!arena) {
                return false;
            }
            auto arena_uintptr = (uintptr)arena;
            auto arena_handles_uintptr = (uintptr)arena_handles_ptr.ptr();
            if (arena_handles_uintptr > arena_uintptr) {
                return false;
            }
            if (!is_aligned(arena_uintptr, alignof(FastArena))) {
                return false;
            }
            auto arena_handle_id = (arena_uintptr - arena_handles_uintptr) / sizeof(FastArena);
            if (arena_handle_id >= num_arenas) {
                return false;
            }

            if (!arena->arena_memory) {
                return false;
            }
            auto arena_memory_uintptr = (uintptr)arena->arena_memory;
            auto arena_data_ptr_uintptr = (uintptr)arena_data_ptr.ptr();
            if (!is_aligned(arena_memory_uintptr, vmem_page_size())) {
                return false;
            }
            if (arena_data_ptr_uintptr < arena_memory_uintptr) {
                return false;
            }
            auto arena_data_id = (arena_memory_uintptr - arena_data_ptr_uintptr) / arena_size; // idiv ... fuck it
            if (arena_data_id >= num_arenas) {
                return false;
            }

            if (arena_data_id != arena_handle_id) {
                return false;
            }

            return true;
        }

        [[nodiscard]] TestRawFastArenaData acquire() {
            if (!list_empty(&free_arenas, FastArenaListOps{})) {
                // arena will be semi-initialized
                // arena_memory will point to its corresponding memory data for convenience
                auto arena = FastArena::list_entry_to_arena(list_pop_head(&free_arenas, FastArenaListOps{}));
                allocated++;
                return {arena->arena_memory, arena};
            }
            if (unused_arena_top < num_arenas) {
                uint arena_id = unused_arena_top++;
                allocated++;
                return _get_arena_by_id(arena_id);
            }
            return {};
        }

        void release(FastArena* arena) {
            CUW3_CHECK(_check_arena(arena), "invalid arena");
            CUW3_CHECK(FastArenaView{arena}.empty(), "arena must be empty");
            
            allocated--;
            list_push_head(&free_arenas, &arena->list_entry, FastArenaListOps{});
        }

        [[nodiscard]] FastArena* locate_arena(void* ptr, uint64 size) const {
            auto arena_data_int = (uintptr)arena_data_ptr.get();
            auto ptr_int = (uintptr)ptr;
            CUW3_CHECK(arena_data_int <= ptr_int && ptr_int < arena_data_int + arena_data_ptr.size(), "invalid pointer");

            auto arena_index = ptr_int / arena_size;
            auto* arena = (FastArena*)advance_arr(arena_data_ptr.get(), arena_size, arena_index); // launder not needed here
            CUW3_CHECK(is_aligned(ptr, FastArenaView{arena}.alignment()), "ptr is misaligned");
            CUW3_CHECK(ptr_int + size < (uintptr)FastArenaView{arena}.data_end(), "ptr goes out of bound");

            return arena;
        }

        bool is_empty() const {
            return allocated == 0;
        }

        VMemPtr arena_data_ptr{};
        VMemPtr arena_handles_ptr{};

        uint num_arenas{};
        uint arena_size{};

        FastArenaListEntry free_arenas{};
        uint unused_arena_top{};
        uint allocated{};
    };

    struct TestFastArenaAllocation {
        explicit operator bool() const {
            return arena;
        }

        FastArena* arena{};
        void* ptr{};
    };

    struct alignas(region_chunk_handle_header_ptr_alignment) TestFastArenaAllocator {
        TestFastArenaAllocator(uint num_arenas, uint arena_size) : arena_storage(num_arenas, arena_size) {
            FastArenaAllocatorConfig allocator_config{};
            allocator_config.memory = &allocator;

            auto& bins_config = allocator_config.bins_config;
            bins_config.num_splits_log2 = 7;
            bins_config.min_arena_step_size_log2 = 12;
            bins_config.max_arena_step_size_log2 = 20;
            bins_config.min_arena_alignment_log2 = 4;
            bins_config.max_arena_alignment_log2 = 9;

            (void)FastArenaAllocator::create(allocator_config);

            CUW3_CHECK(allocator.get_maxmin_alloc_size() <= arena_size, "invalid arena size");
        }
        
        [[nodiscard]] FastArena* _construct_arena(void* arena, void* handle, uint64 alignment) {
            FastArenaConfig config{};
            config.owner = this;
            config.arena_alignment = alignment;
            config.arena_memory = arena;
            config.arena_memory_size = arena_storage.arena_size;
            config.arena_handle = handle;
            config.arena_handle_size = conf_control_block_size;

            auto* created = FastArenaView::create_fast_arena(config);
            CUW3_CHECK(created, "failed to create arena");
            return created;
        }

        [[nodiscard]] TestFastArenaAllocation allocate(uint64 size, uint64 alignment) {
            AcquiredTypedResource<FastArena> acquired = allocator.acquire_arena(size, alignment);
            if (acquired.status_acquired()) {
                return {acquired.get(), allocator.allocate(acquired, size)};
            }
            CUW3_CHECK(!acquired.status_failed(), "something went wrong...");
            
            if (auto raw_arena_data = arena_storage.acquire()) {
                auto* arena = _construct_arena(raw_arena_data.arena, raw_arena_data.handle, alignment);
                return {arena, allocator.allocate(arena, size)};
            }

            return {};
        }

        // return arena ptr belonged, arena can become invalid after deallocation so you may not use it after
        FastArena* deallocate(void* ptr, uint64 size) {
            auto* arena = arena_storage.locate_arena(ptr, size);
            auto* released = allocator.deallocate(arena, ptr, size);
            if (released) {
                arena_storage.release(released);
            }
            return arena;
        }

        // TODO : retire/reclaim


        bool is_allocator_empty() const {
            return allocator._is_allocator_empty() && arena_storage.is_empty();
        }


        uint64 min_alignment() const {
            return allocator.get_min_alignment();
        }

        uint64 max_alignment() const {
            return allocator.get_max_alignment();
        }

        uint64 num_alignments() const {
            return allocator.get_num_alignments();
        }

        uint64 min_alloc_size(uint64 alignment_id) const {
            return allocator.get_min_alloc_size(alignment_id);
        }

        uint64 max_alloc_size() const {
            return allocator.get_max_alloc_size();
        }

        uint64 arena_size() const {
            return arena_storage.arena_size;
        }

        uint64 num_arenas() const {
            return arena_storage.num_arenas;
        }

        uint64 sample_allocation_upper_bound(uint64 alignment_id, uint64 sample_seed) const {
            return allocator._sample_allocation_upper_bound(alignment_id, sample_seed);
        }


        TestFastArenaMemoryStorage arena_storage;
        FastArenaAllocator allocator;
    };

    struct TestFastArenaRandomAllocation {
        bool operator== (const TestFastArenaRandomAllocation& other) const {
            return ptr == other.ptr;
        }

        explicit operator bool() const {
            return ptr;
        }

        void* ptr{};
        uint64 alignment{};
        uint64 size{};
    };

    struct TestFastArenaRandomAllocator : TestFastArenaAllocator {
        TestFastArenaRandomAllocator(uint arena_size, uint num_arenas)
            : TestFastArenaAllocator(num_arenas, arena_size)
            , gen(std::random_device{}())
        {}

        [[nodiscard]] TestFastArenaRandomAllocation allocate() {
            uint64 alignment_id = gen() % num_alignments();
            uint64 alignment = allocator.get_alignment(alignment_id);
            uint64 sample_seed = gen();
            uint64 upper_bound = sample_allocation_upper_bound(alignment_id, sample_seed);
            uint64 lower_bound = min_alloc_size(alignment_id);
            CUW3_CHECK(upper_bound >= lower_bound, "invalid upper bound sampled");

            bool check_expected = true;
            if (upper_bound == 0) {
                check_expected = false;
                upper_bound = arena_size();
            }

            uint64 size = std::uniform_int_distribution<uint64>{lower_bound, upper_bound}(gen);
            auto allocation = TestFastArenaAllocator::allocate(size, alignment);
            CUW3_CHECK(!check_expected || allocation, "failed to make expected allocation");
            return {allocation.ptr, alignment, size};
        }

        std::minstd_rand0 gen;
    };

    void test_fast_arena_bins_location() {
        FastArenaBins bins{};

        FastArenaBinsConfig config{};
        config.memory = &bins;
        config.num_splits_log2 = 7;
        config.min_arena_step_size_log2 = 9;
        config.max_arena_step_size_log2 = 15;
        config.min_arena_alignment_log2 = conf_fast_arena_min_alignment_pow2;
        config.max_arena_alignment_log2 = conf_fast_arena_max_alignment_pow2;

        CUW3_CHECK(FastArenaBins::create(config), "failed to initialize arena");

        // sizes
        {
            uint expected_bin = bins.num_step_splits - 1;
            uint size = bins.get_global_max_alloc_size();
            for (uint step = 0; step < bins.num_steps - 1; step++) {
                uint curr_step = intpow2(bins.max_arena_step_size_log2 - step);
                uint curr_split = divpow2(curr_step, bins.num_splits_log2);
                for (uint split = 0; split < bins.num_splits; split++) {
                    uint curr_size = size - split * curr_split;
                    uint curr_size_mhalf = curr_size - curr_split / 2;
                    uint curr_size_phalf = curr_size + curr_split / 2;
                    uint located_bin = bins.locate_step_split_size(curr_size);
                    uint located_bin_mhalf = bins.locate_step_split_size(curr_size_mhalf);
                    uint located_bin_phalf = bins.locate_step_split_size(curr_size_phalf);
                    uint curr_expected_bin = expected_bin - split;
                    uint curr_expected_bin_mhalf = expected_bin - split;
                    uint curr_expected_bin_phalf = std::min<uint>(expected_bin - split + 1, bins.num_step_splits - 1);
                    CUW3_CHECK(located_bin == curr_expected_bin, "invalid bin located. check test code and/or debug.");
                    CUW3_CHECK(located_bin_mhalf == curr_expected_bin_mhalf, "invalid bin located. check test code and/or debug");
                    CUW3_CHECK(located_bin_phalf == curr_expected_bin_phalf, "invalid bin located. check test code and/or debug");
                }
                size -= curr_step;
                expected_bin -= bins.num_splits;
            }
            uint curr_split = divpow2(intpow2(bins.min_arena_step_size_log2), bins.num_splits_log2);
            for (uint split = 0; split < bins.num_splits; split++) {
                uint curr_size = size - split * curr_split;
                uint curr_size_mhalf = curr_size - curr_split / 2;
                uint curr_size_phalf = curr_size + curr_split / 2;
                uint located_bin = bins.locate_step_split_size(curr_size);
                uint located_bin_mhalf = bins.locate_step_split_size(curr_size_mhalf);
                uint located_bin_phalf = bins.locate_step_split_size(curr_size_phalf);
                uint curr_expected_bin = expected_bin - split;
                uint curr_expected_bin_mhalf = expected_bin - split;
                uint curr_expected_bin_phalf = std::min<uint>(expected_bin - split + 1, bins.num_step_splits - 1);
                CUW3_CHECK(located_bin == curr_expected_bin, "invalid bin located. check test code and/or debug.");
                CUW3_CHECK(located_bin_mhalf == curr_expected_bin_mhalf, "invalid bin located. check test code and/or debug");
                CUW3_CHECK(located_bin_phalf == curr_expected_bin_phalf, "invalid bin located. check test code and/or debug");
            }
        }

        // arenas
        {
            uint expected_bin = bins.num_step_splits - 1;
            uint size = bins.get_global_max_alloc_size();
            for (uint step = 0; step < bins.num_steps - 1; step++) {
                uint curr_step = intpow2(bins.max_arena_step_size_log2 - step);
                uint curr_split = divpow2(curr_step, bins.num_splits_log2);
                for (uint split = 0; split < bins.num_splits; split++) {
                    uint curr_size = size - split * curr_split;
                    uint curr_size_mhalf = curr_size - curr_split / 2;
                    uint curr_size_phalf = curr_size + curr_split / 2;
                    uint located_bin = bins.locate_step_split_arena(curr_size);
                    uint located_bin_mhalf = bins.locate_step_split_arena(curr_size_mhalf);
                    uint located_bin_phalf = bins.locate_step_split_arena(curr_size_phalf);
                    uint curr_expected_bin = expected_bin - split;
                    uint curr_expected_bin_mhalf = expected_bin - split - 1;
                    uint curr_expected_bin_phalf = expected_bin - split;
                    CUW3_CHECK(located_bin == curr_expected_bin, "invalid bin located. check test code and/or debug.");
                    CUW3_CHECK(located_bin_mhalf == curr_expected_bin_mhalf, "invalid bin located. check test code and/or debug");
                    CUW3_CHECK(located_bin_phalf == curr_expected_bin_phalf, "invalid bin located. check test code and/or debug");
                }
                size -= curr_step;
                expected_bin -= bins.num_splits;
            }
            uint curr_split = divpow2(intpow2(bins.min_arena_step_size_log2), bins.num_splits_log2);
            for (uint split = 0; split < bins.num_splits; split++) {
                uint curr_size = size - split * curr_split;
                    uint curr_size_mhalf = curr_size - curr_split / 2;
                    uint curr_size_phalf = curr_size + curr_split / 2;
                    uint located_bin = bins.locate_step_split_arena(curr_size);
                    uint located_bin_mhalf = bins.locate_step_split_arena(curr_size_mhalf);
                    uint located_bin_phalf = bins.locate_step_split_arena(curr_size_phalf);
                    uint curr_expected_bin = expected_bin - split;
                    uint curr_expected_bin_mhalf = expected_bin - split - 1;
                    uint curr_expected_bin_phalf = expected_bin - split;
                    CUW3_CHECK(located_bin == curr_expected_bin, "invalid bin located. check test code and/or debug.");
                    CUW3_CHECK(located_bin_mhalf == curr_expected_bin_mhalf, "invalid bin located. check test code and/or debug");
                    CUW3_CHECK(located_bin_phalf == curr_expected_bin_phalf, "invalid bin located. check test code and/or debug");
            }
        }

        // alignments
        for (uint alignment = 0; alignment < bins.num_alignments; alignment++) {
            uint curr_alignment = intpow2(bins.min_arena_alignment_log2 + alignment);
            uint located_entry = bins.locate_alignment(curr_alignment);
            uint curr_expected_entry = alignment;
            CUW3_CHECK(located_entry == curr_expected_entry, "invalid entry located. check test code and/or debug.");
        }
    }

    template<class T>
    T random_sample_n_pop(std::vector<T>& vec, uint64 seed) {
        CUW3_CHECK(!vec.empty(), "attempt to sample from an empty vector");

        std::swap(vec[seed % vec.size()], vec.back());
        auto elem = std::move(vec.back());
        vec.pop_back();
        return elem;
    }

    void test_fast_arena_allocator_st(uint arena_size, uint num_arenas, uint rounds, uint ops) {
        TestFastArenaRandomAllocator allocator(arena_size, num_arenas);
        std::minstd_rand0 gen{std::random_device{}()};
        std::vector<TestFastArenaRandomAllocation> allocations{};

        for (uint round = 0; round < rounds; round++) {
            uint op = 0;
            while (op < ops) {
                if (gen() % 2 == 0) {
                    // allocate
                    auto allocation = allocator.allocate();
                    if (!allocation) {
                        continue;
                    }
                    allocations.push_back(allocation);
                } else {
                    // deallocate
                    if (allocations.empty()) {
                        continue;
                    }
                    auto allocation = random_sample_n_pop(allocations, gen());
                    allocator.deallocate(allocation.ptr, allocation.size);
                }
                op++;
            }
            while (!allocations.empty()) {
                auto allocation = random_sample_n_pop(allocations, gen());
                allocator.deallocate(allocation.ptr, allocation.size);
            }
            CUW3_CHECK(allocator.is_allocator_empty(), "allocator is not empty");
        }
    }

    void test_fast_arena_allocator_st_max_alloc(uint arena_size, uint num_arenas) {
        // TODO
    }

    void test_fast_arena_allocator_retire_reclaim() {
        // TODO
    }
}

int main() {
    fast_arena_tests::test_arena_full_exaustion(64, 1 << 16);
    fast_arena_tests::test_arena_partial_exaustion1(64);
    fast_arena_tests::test_arena_partial_exaustion2(64);

    fast_arena_allocator_tests::test_fast_arena_bins_location();
    fast_arena_allocator_tests::test_fast_arena_allocator_st();
    fast_arena_allocator_tests::test_fast_arena_allocator_retire_reclaim();
    std::cout << "it's done!" << std::endl;
    return 0;
}