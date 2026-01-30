#include "cuw3/assert.hpp"
#include "cuw3/vmem.hpp"
#include "cuw3/funcs.hpp"
#include "cuw3/fast_arena_allocator.hpp"

#include <memory>
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
    
}

int main() {
    fast_arena_tests::test_arena_full_exaustion(64, 1 << 16);
    fast_arena_tests::test_arena_partial_exaustion1(64);
    fast_arena_tests::test_arena_partial_exaustion2(64);
    std::cout << "it's done!" << std::endl;
    return 0;
}