#include "cuw3/funcs.hpp"
#include "cuw3/atomic.hpp"
#include "cuw3/backoff.hpp"

#include <latch>
#include <vector>
#include <thread>
#include <future>
#include <random>
#include <utility>
#include <iostream>
#include <algorithm>
#include <functional>

using namespace cuw3;

// TODO : atomic list
// TODO : atomic stack
// TODO : atomic push snatch list

template<class Ret>
std::future<Ret> dispatch_job(std::function<Ret()>&& job) {
    std::promise<Ret> promise{};
    std::future<Ret> future = promise.get_future();
    std::thread thread([job = std::move(job), promise = std::move(promise)]() mutable {
        if constexpr(!std::is_same_v<Ret, void>) {
            promise.set_value(job());
        } else {
            promise.set_value();
        }
    });
    thread.detach();
    return future;
}

template<class Ret>
auto dispatch(std::vector<std::function<Ret()>>&& jobs) {
    std::vector<std::future<Ret>> futures;
    for (auto& func : jobs) {
        futures.push_back(dispatch_job(std::move(func)));
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
        // TODO : redundant check
        for (uint i = 1; i < allocations.size(); i++) {
            CUW3_CHECK(allocations[i - 1] < allocations[i], "invalid order of allocated links.");
        }
        for (uint i = 0; i < allocations.size(); i++) {
            CUW3_CHECK(allocations[i] == i, "invalid contents of allocated links.");
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

    struct ListHeadType {
        ListLinkType version : 32{}, next : 32{};
    };

    struct ListTraits {
        using LinkType = ListLinkType;
        using ListHead = ListHeadType;

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

    using ListView = AtomicListView<ListTraits>;

    using ListBackoff = SimpleBackoff;

    struct List : ListTraits {
        List(ListLinkType num_nodes_) {
            head.next = ListTraits::null_link;
            nodes = std::make_unique<ListDataNode[]>(num_nodes_);
            num_nodes = num_nodes_;
        }

        void push(ListLinkType node) {
            ListView{&head}.push(node, ListBackoff{}, ListNodeOps{nodes.get(), num_nodes});
        }

        ListLinkType pop() {
            return ListView{&head}.pop(ListBackoff{}, ListNodeOps{nodes.get(), num_nodes});
        }

        ListDataNode& get(ListLinkType node) {
            return nodes[node];
        }

        bool empty() const {
            return head.next == null_link;
        }

        ListHeadType head{};
        std::unique_ptr<ListDataNode[]> nodes{};
        ListLinkType num_nodes{};
    };

    auto rev_odd_iter(int id) -> int {
        if (id % 2 == 0) {
            return id;
        }
        return id > 0 ? id - 1 : 0;
    }

    auto rev_even_iter(int id) -> int {
        if (id % 2 == 1) {
            return id;
        }
        return id > 0 ? id - 1 : 0;
    }

    // TODO : indices
    //struct StepIter {
    //    int operator* () const {
    //        return index;
    //    }

    //    StepIter& operator++ () {
    //        index += step;
    //        return *this;
    //    }

    //    bool operator== (const StepIter& it) const {
    //        return index == it.index;
    //    }

    //    int index{};
    //    int step{};
    //};

    //struct RevEvenIndices {
    //    RevEvenIndices() {

    //    }

    //    RevEvenIndice

    //    auto begin() {
    //        return StepIter{id_start, -2};
    //    }

    //    auto end() {
    //        return StepIter{id_stop, -2};
    //    }

    //    int id_start{};
    //    int id_stop{};
    //};

    //struct RevOddIndices {
    //    auto begin() {

    //    }

    //    auto end() {

    //    }

    //    int start{};
    //    int stop{};
    //};

    void test_atomic_list_st(ListLinkType num_nodes) {
        List list(num_nodes);

        for (uint i = 0; i < list.num_nodes; i++) {
            list.push(i);
        }
        for (uint i = 0; i < list.num_nodes; i++) {
            auto node = list.pop();
            CUW3_CHECK(node != list.null_link, "invariant violation: list was empty");
            CUW3_CHECK(node == list.num_nodes - 1 - i, "invariant violation: we popped the wrong node");
        }
        CUW3_CHECK(list.empty(), "invariant violation: list was not empty");

        for (uint i = 0; i < list.num_nodes; i += 2) {
            list.push(i);
        }
        for (auto i = rev_even_iter(list.num_nodes); i > 0; i -= 2) {
            auto node = list.pop();
            CUW3_CHECK(node != list.null_link, "invariant violation: list was empty");
            CUW3_CHECK(node == i - 1, "invariant violation: we popped the wrong node");
        }
        CUW3_CHECK(list.empty(), "invariant violation: list was not empty");

        for (uint i = 1; i < list.num_nodes; i += 2) {
            list.push(i);
        }
        for (auto i = rev_odd_iter(list.num_nodes); i > 0; i -= 2) {
            auto node = list.pop();
            CUW3_CHECK(node != list.null_link, "invariant violation: list was empty");
            CUW3_CHECK(node == i - 1, "invariant violation: we popped the wrong node");
        }
        CUW3_CHECK(list.empty(), "invariant violation: list was not empty");

        for (uint i = 0; i < list.num_nodes; i += 2) {
            list.push(i);
        }
        for (uint i = 1; i < list.num_nodes; i += 2) {
            list.push(i);
        }
        for (auto i = rev_odd_iter(list.num_nodes); i > 0; i -= 2) {
            auto node = list.pop();
            CUW3_CHECK(node != list.null_link, "invariant violation: list was empty");
            CUW3_CHECK(node == i - 1, "invariant violation: we popped the wrong node");
        }
        for (auto i = rev_even_iter(list.num_nodes); i > 0; i -= 2) {
            auto node = list.pop();
            CUW3_CHECK(node != list.null_link, "invariant violation: list was empty");
            CUW3_CHECK(node == i - 1, "invariant violation: we popped the wrong node");
        }
        CUW3_CHECK(list.empty(), "invariant violation: list was not empty");
    }

    struct JobPart {
        uint start{};
        uint stop{};
    };

    auto get_job_part(uint start, uint stop, uint parts, uint part_id) -> JobPart {
        uint delta = stop - start + parts - 1;
        delta -= delta % parts;
        uint part = delta / parts;
        uint job_start = std::clamp(part * part_id, start, stop);
        uint job_stop = std::clamp(job_start + part, start, stop);
        return {job_start, job_stop};
    }

    void test_atomic_list_mt(uint num_nodes, uint num_threads) {
        num_nodes += num_threads - 1;
        num_nodes -= num_nodes % num_threads;

        List list(num_nodes);

        struct ThreadResult {
            uint thread_id{};
            std::vector<ListLinkType> allocated{};
        };

        std::latch prepush(num_threads);
        std::vector<std::function<ThreadResult()>> jobs{};
        for (uint i = 0; i < num_threads; i++) {
            auto job_part = get_job_part(0, num_nodes, num_threads, i);
            jobs.push_back([thread_id = i, job_part, &list, &prepush] () -> ThreadResult {
                // TODO
                return {};
            });
        }

        auto thread_results = dispatch(std::move(jobs));

        uint total_allocated = 0;
        for (auto& result : thread_results) {
            total_allocated += result.allocated.size();
        }
        CUW3_CHECK(total_allocated == num_nodes, "invariant violation: total allocated amount of nodes does not equal to max number of nodes.");

        std::vector<ListLinkType> allocated{};
        allocated.reserve(num_nodes);
        for (auto& result : thread_results) {
            allocated.insert(allocated.end(), result.allocated.begin(), result.allocated.end());
        }
        std::sort(allocated.begin(), allocated.end());

        // TODO : redundant check
        for (uint i = 1; i < allocated.size(); i++) {
            CUW3_CHECK(allocated[i - 1] < allocated[i], "invariant violation: repeating allocations found");
        }
        for (uint i = 0; i < allocated.size(); i++) {
            CUW3_CHECK(allocated[i] == i, "invariant violation: contents of allocations is not full");
        }
    }
}

int main() {
    test_dispatch();

    atomic_stack_tests::test_atomic_stack_st(10000);
    for (int i = 0; i < 16; i++) {
        atomic_stack_tests::test_atomic_stack_mt(10000, 8);
    }

    atomic_list_tests::test_atomic_list_st(10000);
    atomic_list_tests::test_atomic_list_st(10001);

    return 0;
}