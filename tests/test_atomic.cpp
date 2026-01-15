#include "cuw3/atomic.hpp"

#include <latch>
#include <vector>
#include <thread>
#include <future>
#include <utility>
#include <iostream>
#include <algorithm>
#include <functional>

using namespace cuw3;

// TODO : atomic list
// TODO : atomic stack
// TODO : atomic push snatch list

template<class Ret>
auto dispatch(std::vector<std::function<Ret()>>&& functions) {
    std::vector<std::future<Ret>> futures;
    for (auto& func : functions) {
        futures.push_back(std::async(std::launch::async, std::move(func)));
    }
    if constexpr(!std::is_same_v<Ret, void>) {
        std::vector<Ret> results{};
        for (auto& future : futures) {
            results.push_back(future.get());
        }
        return results;
    } else {
        for (auto& future : futures) {
            future.get();
        }
        return;
    }
}

void test_dispatch() {
    std::vector<std::function<int()>> functions_int{};
    for (int i = 0; i < 16; i++) {
        functions_int.push_back([i] () {
            return i;
        });
    }
    auto results = dispatch(std::move(functions_int));
    for (auto result : results) {
        std::cout << result << " ";
    }
    std::cout << std::endl;

    std::vector<std::function<void()>> functions_void{};
    for (int i = 0; i < 16; i++) {
        functions_void.push_back([] () {
            return;
        });
    }
    dispatch(std::move(functions_void));
}

namespace atomic_stack_tests {
    using StackLinkType = uint64;

    struct StackTraits {
        using LinkType = StackLinkType;

        static constexpr LinkType null_link = 0xFFFFFFFF;
        static constexpr LinkType op_failed = 0xFFFFFFFE;
    };

    struct Stack {
        StackLinkType top{};
        StackLinkType limit{};
    };

    using StackView = AtomicBumpStackView<StackTraits>;

    void check_stack_allocations(const std::vector<StackLinkType>& allocations, const Stack& stack) {
        CUW3_CHECK(allocations.size() == stack.limit, "invalid count of allocations");
        for (uint i = 1; i < allocations.size(); i++) {
            if (allocations[i - 1] >= allocations[i]) {
                CUW3_ABORT("invalid order of allocated links");
            }
        }
    }

    void test_atomic_stack_st(uint count) {
        Stack stack{0, count};

        std::vector<StackLinkType> allocated;
        allocated.reserve(count);

        while (true) {
            auto view = StackView{&stack.top, stack.limit};
            auto top = view.bump();
            if (top == view.null_link) {
                break;
            }
            allocated.push_back(top);
        }
        check_stack_allocations(allocated, stack);
    }

    void test_atomic_stack_mt(uint count, uint threads) {
        count = (count + threads - 1);
        count -= count % threads;

        Stack stack{0, count};

        struct ThreadResult {
            uint thread_id{};
            std::vector<StackLinkType> allocated{};
        };

        std::latch latch{threads}; 
        std::vector<std::function<ThreadResult()>> jobs{};
        for (uint i = 0; i < threads; i++) {
            jobs.push_back([i, &latch, &stack] () -> ThreadResult {
                latch.arrive_and_wait();

                ThreadResult result{i};
                while (true) {
                    auto view = StackView{&stack.top, stack.limit};
                    auto top = view.bump();
                    if (top == view.null_link) {
                        break;
                    }
                    result.allocated.push_back(top);
                }
                return result;
            });
        }

        auto thread_results = dispatch(std::move(jobs));

        uint sum = 0;
        for (auto& result : thread_results) {
            sum += result.allocated.size();
        }
        CUW3_CHECK(sum == stack.limit, "invalid count of allocations");

        std::vector<StackLinkType> allocations{};
        allocations.reserve(sum);
        for (auto& result : thread_results) {
            allocations.insert(allocations.end(), result.allocated.begin(), result.allocated.end());
        }
        std::sort(allocations.begin(), allocations.end());

        check_stack_allocations(allocations, stack);
    }
}

namespace atomic_list_tests {
    using ListLinkType = uint64;

    struct ListDataNode {
        ListLinkType next{};
        uint64 data{};
    };

    struct ListTraits {
        using LinkType = ListLinkType;

        struct ListHead {
            LinkType version: 32{}, next: 32{};
        };

        static constexpr LinkType null_link = 0xFFFFFFFF;
        static constexpr LinkType op_failed = 0xFFFFFFFE;
    };

    struct ListNodeOps {
        void set_next(ListLinkType node, ListLinkType next) {
            CUW3_CHECK(node < num_nodes, "invalid node id received");

            std::atomic_ref{nodes[node].next}.store(next, std::memory_order_relaxed);
        }

        ListLinkType get_next(ListLinkType node) {
            CUW3_CHECK(node < num_nodes, "invalid node id received");

            return std::atomic_ref{nodes[node].next}.load(std::memory_order_relaxed);
        }

        ListDataNode* nodes{};
        ListLinkType num_nodes{};
    };

    void test_atomic_list_st() {
        // TODO
    }

    void test_atomic_list_mt() {
        // TODO
    }
}

int main() {
    test_dispatch();

    atomic_stack_tests::test_atomic_stack_st(10000);
    for (int i = 0; i < 16; i++) {
        atomic_stack_tests::test_atomic_stack_mt(10000, 8);
    }

    return 0;
}