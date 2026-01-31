#include "cuw3/funcs.hpp"
#include "cuw3/atomic.hpp"
#include "cuw3/backoff.hpp"

#include <latch>
#include <mutex>
#include <vector>
#include <thread>
#include <future>
#include <random>
#include <utility>
#include <numeric>
#include <barrier>
#include <iostream>
#include <algorithm>
#include <functional>
#include <condition_variable>

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

    struct ListNodeLabel {
        uint32 thread{};
        uint32 node{};
    };

    struct ListDataNode {
        void store(ListNodeLabel d) {
            std::atomic_ref{data}.store(data, std::memory_order_relaxed);
        }

        ListNodeLabel load() const {
            std::atomic_ref{data}.load(std::memory_order_relaxed);
        }

        ListLinkType next{};
        ListNodeLabel data{};
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

        void push(ListLinkType node, ListNodeLabel label) {
            nodes[node].store(label);
            ListView{&head}.push(node, ListBackoff{}, ListNodeOps{nodes.get(), num_nodes});
        }

        [[nodiscard]] ListLinkType pop(ListNodeLabel label) {
            auto popped = ListView{&head}.pop(ListBackoff{}, ListNodeOps{nodes.get(), num_nodes});
            if (popped != null_link) {
                nodes[popped].store(label);
            }
            return popped;
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
            list.push(i, {0, i});
        }
        for (uint i = 0; i < list.num_nodes; i++) {
            auto node = list.pop({0, i});
            CUW3_CHECK(node != list.null_link, "invariant violation: list was empty");
            CUW3_CHECK(node == list.num_nodes - 1 - i, "invariant violation: we popped the wrong node");
        }
        CUW3_CHECK(list.empty(), "invariant violation: list was not empty");

        for (uint i = 0; i < list.num_nodes; i += 2) {
            list.push(i, {0, i});
        }
        for (auto i = rev_even_iter(list.num_nodes); i > 0; i -= 2) {
            auto node = list.pop({0, (uint)i});
            CUW3_CHECK(node != list.null_link, "invariant violation: list was empty");
            CUW3_CHECK(node == i - 1, "invariant violation: we popped the wrong node");
        }
        CUW3_CHECK(list.empty(), "invariant violation: list was not empty");

        for (uint i = 1; i < list.num_nodes; i += 2) {
            list.push(i, {0, i});
        }
        for (auto i = rev_odd_iter(list.num_nodes); i > 0; i -= 2) {
            auto node = list.pop({0, (uint)i});
            CUW3_CHECK(node != list.null_link, "invariant violation: list was empty");
            CUW3_CHECK(node == i - 1, "invariant violation: we popped the wrong node");
        }
        CUW3_CHECK(list.empty(), "invariant violation: list was not empty");

        for (uint i = 0; i < list.num_nodes; i += 2) {
            list.push(i, {0, i});
        }
        for (uint i = 1; i < list.num_nodes; i += 2) {
            list.push(i, {0, i});
        }
        for (auto i = rev_odd_iter(list.num_nodes); i > 0; i -= 2) {
            auto node = list.pop({0, (uint)i});
            CUW3_CHECK(node != list.null_link, "invariant violation: list was empty");
            CUW3_CHECK(node == i - 1, "invariant violation: we popped the wrong node");
        }
        for (auto i = rev_even_iter(list.num_nodes); i > 0; i -= 2) {
            auto node = list.pop({0, (uint)i});
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

    // TODO : structural tests
    // TODO : set data and check data
    //void test_atomic_list_mt(uint num_nodes, uint num_threads, uint num_runs, uint num_ops) {
    //    num_nodes += num_threads - 1;
    //    num_nodes -= num_nodes % num_threads;

    //    List list(num_nodes);

    //    struct alignas(64) ThreadContext {
    //        uint thread_id{};
    //        JobPart job_part{};
    //        std::vector<ListLinkType> popped{};
    //    };
    //    std::vector<ThreadContext> threads_contexts{num_threads};

    //    std::vector<bool> visited{};
    //    auto barrier_structure_test = [&]() {
    //        visited.clear();
    //        visited.resize(num_nodes, 0);

    //        uint curr = list.head.next;
    //        while (curr != list.null_link) {
    //            auto& node = list.get(curr);
    //            visited[curr] = true;
    //            CUW3_CHECK(curr != node.next, "cycle");

    //            curr = node.next;
    //        }
    //        for (auto v : visited) {
    //            CUW3_CHECK(v, "not all nodes were pushed");
    //        }
    //    };

    //    std::barrier barrier(num_threads, barrier_structure_test);

    //    std::vector<std::function<void()>> jobs{};
    //    for (uint i = 0; i < num_threads; i++) {
    //        auto job_part = get_job_part(0, num_nodes, num_threads, i);
    //        jobs.push_back([thread_id = i, job_part, num_ops, &list, &prepush] () -> ThreadResult {
    //            ThreadResult result{thread_id};
    //            std::minstd_rand rand{std::random_device{}()};

    //            for (uint i = job_part.start; i < job_part.stop; i++) {
    //                list.push(i);
    //            }

    //            prepush.arrive_and_wait();
    //            
    //            uint op = 0;
    //            while (op < num_ops) {
    //                auto choice = rand() % 2;
    //                if (choice) {
    //                    auto node = list.pop();
    //                    if (node != list.null_link) {
    //                        result.allocated.push_back(node);
    //                    } else {
    //                        continue;
    //                    }
    //                } else {
    //                    if (!result.allocated.empty()) {
    //                        list.push(result.allocated.back());
    //                        result.allocated.pop_back();
    //                    } else {
    //                        continue;
    //                    }
    //                }
    //                op++;
    //            }

    //            while (true) {
    //                auto node = list.pop();
    //                if (node == list.null_link) {
    //                    break;
    //                }
    //                result.allocated.push_back(node);
    //            }

    //            return result;
    //        });
    //    }

    //    dispatch(std::move(jobs));

    //    uint total_allocated = 0;
    //    for (auto& thread_context : threads_contexts) {
    //        total_allocated += thread_context.popped.size();
    //    }
    //    CUW3_CHECK(total_allocated == num_nodes, "invariant violation: total allocated amount of nodes does not equal to max number of nodes.");

    //    std::vector<ListLinkType> allocated{};
    //    allocated.reserve(num_nodes);
    //    for (auto& thread_context : threads_contexts) {
    //        allocated.insert(allocated.end(), thread_context.popped.begin(), thread_context.popped.end());
    //    }
    //    std::sort(allocated.begin(), allocated.end());
    //    
    //    // TODO : redundant check
    //    for (uint i = 1; i < allocated.size(); i++) {
    //        CUW3_CHECK(allocated[i - 1] < allocated[i], "invariant violation: repeating allocations found");
    //    } 
    //    for (uint i = 0; i < allocated.size(); i++) {
    //        CUW3_CHECK(allocated[i] == i, "invariant violation: contents of allocations is not full");
    //    }
    //}
}

namespace atomic_push_snatch_tests {
    struct ListNode {
        ListNode* next{};
        ListNode* skip{};
        uint64 data{};
    };

    struct ListTraits {
        using LinkType = ListNode*;

        static constexpr LinkType null_link = nullptr;
    };

    struct ListNodeOps {
        ListNode* get_skip(ListNode* node) {
            return node->skip;
        }

        void set_skip(ListNode* node, ListNode* skip) {
            node->skip = skip;
        }

        void reset_skip(ListNode* node) {
            node->skip = nullptr;
        }

        ListNode* get_next(ListNode* node) {
            return node->next;
        }

        void set_next(ListNode* node, ListNode* next) {
            node->next = next;
        }

        void reset_next(ListNode* node) {
            node->next = nullptr;
        }
    };

    using ListView = AtomicPushSnatchList<ListTraits>;

    using Backoff = SimpleBackoff;

    struct ListPart {
        bool valid_list_node(ListNode* node) const {
            if (!node) {
                return false;
            }
            if (node->next == nullptr && node->skip != node) {
                return false;
            }
            return true;
        }

        bool valid_single_node(ListNode* node) const {
            if (!node) {
                return false;
            }
            if (node->next != nullptr || node->skip != node) {
                return false;
            }
            return true;
        }

        void push(ListNode* node) {
            CUW3_CHECK(valid_single_node(node), "node must be single node");

            push(ListPart{node});
        }

        void push(ListPart&& part) {
            ListView{&head}.push(part.head, Backoff{}, ListNodeOps{});
        }

        [[nodiscard]] ListPart snatch() {
            return ListPart{ListView{&head}.snatch()};
        }

        ListNode* get_skip(ListNode* node) const {
            return ListView::get_tail(node, ListNodeOps{});
        }

        ListNode* get_tail(ListNode* node) const {
            return ListView::get_tail(node, ListNodeOps{});
        }

        void reset() {
            head = nullptr;
        }

        bool empty() const {
            return head;
        }

        ListNode* head{};
    };

    struct List : ListPart, ListTraits {
        List(uint num_nodes_) {
            num_nodes = num_nodes_;
            nodes = std::make_unique<ListNode[]>(num_nodes);
        }

        ListNode* get_node(uint id) const {
            return &nodes[id];
        }

        ListNode* get_node_init(uint id, uint data) const {
            auto* node = get_node(id);
            node->next = nullptr;
            node->skip = node;
            node->data = data;
            return node;
        }

        uint get_node_id(ListNode* node) const {
            CUW3_CHECK(node, "node must not be nullptr");
            CUW3_CHECK((uintptr)node >= (uintptr)nodes.get(), "invalid node given");

            uint id = node - nodes.get();
            CUW3_CHECK(id < num_nodes, "node id is invalid");

            return id;
        }

        std::unique_ptr<ListNode[]> nodes{};
        uint num_nodes{};
    };

    void test_atomic_push_snatch_list_st(uint num_nodes) {
        std::vector<ListNode*> traversed{};
        List list{num_nodes};

        list.reset();
        traversed.clear();
        for (uint i = 0; i < list.num_nodes; i++) {
            list.push(list.get_node_init(i, i));
        }
        {
            ListNode* curr = list.head;
            while (curr) {
                traversed.push_back(curr);
                curr = curr->next;
            }

            CUW3_CHECK(traversed.size() == list.num_nodes, "invalid amount of nodes");
            for (uint i = 1; i < traversed.size(); i++) {
                auto prev = list.get_node_id(traversed[i - 1]);
                auto curr = list.get_node_id(traversed[  i  ]);
                CUW3_CHECK(prev > curr, "invalid list structure");
            }
        }

        list.reset();
        traversed.clear();
        for (uint i = list.num_nodes; i > 0; i--) {
            list.push(list.get_node_init(i - 1, i - 1));
        }
        {
            ListNode* curr = list.head;
            while (curr) {
                traversed.push_back(curr);
                curr = curr->next;
            }

            CUW3_CHECK(traversed.size() == list.num_nodes, "invalid amount of nodes");
            for (uint i = 1; i < traversed.size(); i++) {
                auto prev = list.get_node_id(traversed[i - 1]);
                auto curr = list.get_node_id(traversed[i]);
                CUW3_CHECK(prev < curr, "invalid list structure");
            }
        }
    }

    void test_atomic_push_snatch_structure_st() {
        List list(12);
        
        auto n0 = list.get_node_init(0, 0);
        auto n1 = list.get_node_init(1, 1);
        auto n2 = list.get_node_init(2, 2);
        ListPart part1{};
        part1.push(n2);
        part1.push(n1);
        part1.push(n0);
        n0->skip = n2;
        n1->skip = n2;
        n2->skip = n2;

        auto n3 = list.get_node_init(3, 3);
        auto n4 = list.get_node_init(4, 4);
        auto n5 = list.get_node_init(5, 5);
        ListPart part2{};
        part2.push(n5);
        part2.push(n4);
        part2.push(n3);
        n3->skip = n5;
        n4->skip = n5;
        n5->skip = n5;

        auto n6 = list.get_node_init(6, 6);
        auto n7 = list.get_node_init(7, 7);
        auto n8 = list.get_node_init(8, 8);
        auto n9 = list.get_node_init(9, 9);
        auto n10 = list.get_node_init(10, 10);
        auto n11 = list.get_node_init(11, 11);
        ListPart part3{};
        part3.push(n11);
        part3.push(n10);
        part3.push(n9);
        part3.push(n8);
        part3.push(n7);
        part3.push(n6);
        n6->skip = n8;
        n7->skip = n11;
        n8->skip = n9;
        n9->skip = n11;
        n10->skip = n11;
        n11->skip = n11;

        list.reset();
        list.push(std::move(part3));
        list.push(std::move(part2));
        list.push(std::move(part1));

        std::vector<uint> nodes{};
        std::vector<uint> tails{};
        {
            ListNode* curr = list.head;
            while (curr) {
                nodes.push_back(list.get_node_id(curr));
                tails.push_back(list.get_node_id(curr->skip));
                curr = curr->next;
            }
        }

        std::vector<uint> expected_nodes{};
        {
            auto result = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
            expected_nodes.insert(expected_nodes.end(), result.begin(), result.end());
        }

        std::vector<uint> expected_tails{};
        {
            auto result = {2, 2, 2, 5, 5, 5, 11, 11, 9, 11, 11, 11};
            expected_tails.insert(expected_tails.end(), result.begin(), result.end());
        }

        CUW3_CHECK(nodes == expected_nodes, "not exactly");
        CUW3_CHECK(tails == expected_tails, "not exactly");
    }

    void test_atomic_push_snatch_list_mt() {
        // TODO
    }
}

int main() {
    std::cout << "test dispatch..." << std::endl;
    test_dispatch();

    std::cout << "test_atomic_stack_st..." << std::endl;
    atomic_stack_tests::test_atomic_stack_st(10000);
    for (int i = 0; i < 16; i++) {
        std::cout << "test_atomic_stack_mt " << i << " ..." << std::endl;
        atomic_stack_tests::test_atomic_stack_mt(10000, 8);
    }

    std::cout << "test_atomic_list_st..." << std::endl;
    atomic_list_tests::test_atomic_list_st(10000);
    std::cout << "test_atomic_list_st..." << std::endl;
    atomic_list_tests::test_atomic_list_st(10001);
    /*for (int i = 0; i < 16; i++) {
        std::cout << "test_atomic_list_mt " << i << " ..." << std::endl;
        atomic_list_tests::test_atomic_list_mt(10000, 8, 100000);
    }*/
    atomic_push_snatch_tests::test_atomic_push_snatch_list_st(1000);
    atomic_push_snatch_tests::test_atomic_push_snatch_structure_st();
    atomic_push_snatch_tests::test_atomic_push_snatch_list_mt();

    std::cout << "done!" << std::endl;
    
    return 0;
}