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
    std::thread thread([captured_job = std::move(job), captured_promise = std::move(promise)]() mutable {
        if constexpr(!std::is_same_v<Ret, void>) {
            captured_promise.set_value(captured_job());
        } else {
            captured_job();
            captured_promise.set_value();
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
        bool operator==(const ListNodeLabel&) const = default;

        uint32 node{};
        uint32 thread{};
    };

    struct ListDataNode {
        void store(ListNodeLabel d) {
            std::atomic_ref{data}.store(d, std::memory_order_relaxed);
        }

        ListNodeLabel load() const {
            return std::atomic_ref{data}.load(std::memory_order_relaxed);
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

        void push(ListLinkType node, uint32 thread_id) {
            nodes[node].store({(uint32)node, thread_id});
            ListView{&head}.push(node, ListBackoff{}, ListNodeOps{nodes.get(), num_nodes});
        }

        [[nodiscard]] ListLinkType pop(uint32 thread_id) {
            auto popped = ListView{&head}.pop(ListBackoff{}, ListNodeOps{nodes.get(), num_nodes});
            if (popped != null_link) {
                nodes[popped].store({(uint32)popped, thread_id});
            }
            return popped;
        }

        ListDataNode& get(ListLinkType node) {
            return nodes[node];
        }

        bool empty() const {
            return head.next == null_link;
        }

        template<class Func>
        void traverse(Func&& func) {
            auto node_ops = ListNodeOps{nodes.get(), num_nodes};

            ListLinkType curr = head.next;
            while (curr != null_link) {
                auto& node = get(curr);
                func(curr, node);
                curr = node_ops.get_next(curr);
            }
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

    void test_atomic_list_st(ListLinkType num_nodes) {
        List list(num_nodes);

        for (uint i = 0; i < list.num_nodes; i++) {
            list.push(i, 0);
        }
        for (uint i = 0; i < list.num_nodes; i++) {
            auto node = list.pop(0);
            CUW3_CHECK(node != list.null_link, "invariant violation: list was empty");
            CUW3_CHECK(node == list.num_nodes - 1 - i, "invariant violation: we popped the wrong node");
        }
        CUW3_CHECK(list.empty(), "invariant violation: list was not empty");

        for (uint i = 0; i < list.num_nodes; i += 2) {
            list.push(i, 0);
        }
        for (auto i = rev_even_iter(list.num_nodes); i > 0; i -= 2) {
            auto node = list.pop(0);
            CUW3_CHECK(node != list.null_link, "invariant violation: list was empty");
            CUW3_CHECK(node == i - 1, "invariant violation: we popped the wrong node");
        }
        CUW3_CHECK(list.empty(), "invariant violation: list was not empty");

        for (uint i = 1; i < list.num_nodes; i += 2) {
            list.push(i, 0);
        }
        for (auto i = rev_odd_iter(list.num_nodes); i > 0; i -= 2) {
            auto node = list.pop(0);
            CUW3_CHECK(node != list.null_link, "invariant violation: list was empty");
            CUW3_CHECK(node == i - 1, "invariant violation: we popped the wrong node");
        }
        CUW3_CHECK(list.empty(), "invariant violation: list was not empty");

        for (uint i = 0; i < list.num_nodes; i += 2) {
            list.push(i, 0);
        }
        for (uint i = 1; i < list.num_nodes; i += 2) {
            list.push(i, 0);
        }
        for (auto i = rev_odd_iter(list.num_nodes); i > 0; i -= 2) {
            auto node = list.pop(0);
            CUW3_CHECK(node != list.null_link, "invariant violation: list was empty");
            CUW3_CHECK(node == i - 1, "invariant violation: we popped the wrong node");
        }
        for (auto i = rev_even_iter(list.num_nodes); i > 0; i -= 2) {
            auto node = list.pop(0);
            CUW3_CHECK(node != list.null_link, "invariant violation: list was empty");
            CUW3_CHECK(node == i - 1, "invariant violation: we popped the wrong node");
        }
        CUW3_CHECK(list.empty(), "invariant violation: list was not empty");
    }

    void test_atomic_list_mt(uint num_nodes, uint num_threads, uint num_runs, uint num_ops) {
        num_nodes += num_threads - 1;
        num_nodes -= num_nodes % num_threads;

        List list(num_nodes);

        struct alignas(64) ThreadContext {
            std::vector<ListLinkType> popped{};
        };
        std::vector<ThreadContext> threads_contexts{num_threads};

        std::vector<bool> visited{};
        auto barrier_structure_test = [&]() {
            auto check_list = [&] (List& list, uint32 thread_id) {
                auto check = [&] (ListLinkType id, ListDataNode& node) {
                    auto label = ListNodeLabel{(uint32)id, thread_id};
                    CUW3_CHECK(!visited[id], "already visited");
                    CUW3_CHECK(node.load() == label, "invalid list label");
                    CUW3_CHECK(id != node.next, "cycle detected");
                    
                    visited[id] = true;
                };
                list.traverse(check);
            };

            visited.clear();
            visited.resize(num_nodes, 0);

            check_list(list, num_threads);
            for (uint thread_id = 0; thread_id < num_threads; thread_id++) {
                auto& context = threads_contexts[thread_id];
                for (auto node : context.popped) {
                    CUW3_CHECK(!visited[node], "node has been popped and pushed at the same time");

                    visited[node] = true;

                    auto& node_ref = list.get(node);
                    auto thread_label = ListNodeLabel{(uint32)node, (uint32)thread_id};
                    CUW3_CHECK(node_ref.data == thread_label, "invalid label set on popped node");
                }
            }
            for (auto v : visited) {
                CUW3_CHECK(v, "not all nodes were pushed");
            }
        };

        std::barrier barrier(num_threads, barrier_structure_test);
        std::latch meetup(num_threads);

        std::vector<std::function<void()>> jobs{};
        for (uint i = 0; i < num_threads; i++) {
            auto job_part = get_job_part(0, num_nodes, num_threads, i);
            jobs.push_back([&, thread_id = i, job_part] () {
                std::minstd_rand rand{std::random_device{}()};

                for (uint i = job_part.start; i < job_part.stop; i++) {
                    list.push(i, num_threads);
                }
                meetup.arrive_and_wait();
                
                for (uint run = 0; run < num_runs; run++) {
                    auto& context = threads_contexts[thread_id];

                    uint op = 0;
                    while (op < num_ops) {
                        auto choice = rand() % 2;
                        if (choice) { // pop
                            auto node = list.pop(thread_id);
                            if (node != list.null_link) {
                                context.popped.push_back(node);
                            } else {
                                continue;
                            }
                        } else { // push
                            if (!context.popped.empty()) {
                                list.push(context.popped.back(), num_threads);
                                context.popped.pop_back();
                            } else {
                                continue;
                            }
                        }
                        op++;
                    }

                    barrier.arrive_and_wait();
    
                    while (true) {
                        auto node = list.pop(thread_id);
                        if (node == list.null_link) {
                            break;
                        }
                        context.popped.push_back(node);
                    }

                    barrier.arrive_and_wait();
                }
            });
        }
        dispatch(std::move(jobs));
    }
}

namespace atomic_push_snatch_tests {
    struct ListNodeLabel {
        bool operator== (const ListNodeLabel&) const = default;

        uint32 node_id{};
        uint32 thread_id{};
    };

    struct ListNode {
        void store(ListNodeLabel new_label) {
            std::atomic_ref{label}.store(new_label, std::memory_order_relaxed);
        }

        ListNodeLabel load() {
            return std::atomic_ref{label}.load(std::memory_order_relaxed);
        }

        ListNode* next{};
        ListNode* skip{};
        ListNodeLabel label{};
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
            ListView{&head}.push(std::exchange(part.head, nullptr), Backoff{}, ListNodeOps{});
        }

        [[nodiscard]] ListPart snatch() {
            return ListPart{ListView{&head}.snatch()};
        }

        [[nodiscard]] ListPart snatch_part(uint amount) {
            return ListPart{ListView{&head}.snatch_part(amount, Backoff{}, ListNodeOps{})};
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
            return !head;
        }

        template<class Func>
        void traverse(Func&& func) {
            auto* curr = head;
            while (curr) {
                func(curr);
                curr = curr->next;
            }
        }

        ListNode* head{};
    };

    struct List : ListPart, ListTraits {
        List(uint num_nodes_) {
            num_nodes = num_nodes_;
            nodes = std::make_unique<ListNode[]>(num_nodes);
        }

        void push_n_label(ListNode* node, uint32 thread_id = 0) {
            node->store({get_node_id(node), thread_id});
            ListPart::push(node);
        }

        void push_n_label(ListPart&& part, uint32 thread_id = 0) {
            part.traverse([&] (ListNode* node) {
                node->store({get_node_id(node), thread_id});
            });
            ListPart::push(std::move(part));
        }

        [[nodiscard]] ListPart snatch_part_n_label(uint part_size, uint32 thread_id = 0) {
            auto part = ListPart::snatch_part(part_size);
            part.traverse([&](ListNode* node) {
                node->store({get_node_id(node), thread_id});
            });
            return part;
            
            // ListPart snatched = ListPart::snatch();

            // auto bite = [] (ListPart&& part, uint count) -> std::pair<ListPart, ListPart> {
            //     ListPart part1{std::exchange(part.head, nullptr)};
            //     ListNode* tail = part.head;
            //     if (!tail) {
            //         return {};
            //     }
            //     for (uint i = 0; i < count && tail->next; i++) {
            //         tail = tail->next;
            //     }

            //     ListPart part2{std::exchange(tail->next, nullptr)};

            //     ListNode* curr = part1.head;
            //     while (curr) {
            //         curr->skip = tail;
            //         curr = curr->next;
            //     }
            //     return {part1, part2};
            // };

            // auto [part1, part2] = bite(std::move(snatched), part_size);
            // ListPart::push(std::move(part2));
            // part1.traverse([&] (ListNode* node) {
            //     node->store({get_node_id(node), thread_id});
            // });
            // return part1;
        }

        ListNode* get_node(uint id) const {
            return &nodes[id];
        }

        ListNode* get_node_init(uint32 id, uint32 thread_id) const {
            auto* node = get_node(id);
            node->next = nullptr;
            node->skip = node;
            node->label = {id, thread_id};
            return node;
        }

        uint32 get_node_id(ListNode* node) const {
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

    void test_atomic_push_snatch_list_mt(uint num_nodes, uint num_threads, uint num_runs, uint num_ops) {
        num_nodes = num_nodes + num_threads - 1;
        num_nodes -= num_nodes % num_threads;

        List list(num_nodes);

        struct ThreadContext {
            uint thread_id{};
            JobPart job_part{};
            std::vector<ListPart> list_parts{};
        };
        std::vector<ThreadContext> threads_contexts(num_threads);

        std::vector<bool> visited{};
        auto barrier_structure_test = [&] () {
            auto check_list_part = [&] (ListPart list_part, uint32 thread_id) {
                auto check = [&] (ListNode* node) {
                    auto node_id = list.get_node_id(node);
                    auto label = ListNodeLabel{node_id, thread_id};
                    CUW3_CHECK(!visited[node_id], "already visited");
                    CUW3_CHECK(node->load() == label, "invalid list label");
                    CUW3_CHECK(node != node->next, "cycle detected");
                    
                    visited[node_id] = true;
                };
                list_part.traverse(check);
            };

            visited.clear();
            visited.resize(num_nodes, false);

            check_list_part(list, num_threads);
            for (uint thread_id = 0; thread_id < num_threads; thread_id++) {
                auto& context = threads_contexts[thread_id];
                for (auto list_part : context.list_parts) {
                    check_list_part(list_part, thread_id);
                }
            }
            for (auto v : visited) {
                CUW3_CHECK(v, "not all nodes were visited");
            }
        };

        std::barrier barrier(num_threads, barrier_structure_test);

        std::vector<std::function<void()>> jobs{};
        for (uint thread_id = 0; thread_id < num_threads; thread_id++) {
            auto job_part = get_job_part(0, num_nodes, num_threads, thread_id);
            jobs.push_back([&, thread_id, job_part] () {
                auto rand = std::minstd_rand{std::random_device{}()};

                for (uint node_id = job_part.start; node_id < job_part.stop; node_id++) {
                    list.push_n_label(list.get_node_init(node_id, num_threads), num_threads);
                }

                barrier.arrive_and_wait();

                auto& context = threads_contexts[thread_id];
                for (uint run = 0; run < num_runs; run++) {
                    uint ops = 0;
                    while (ops < num_ops) {
                        uint op = rand() % 2;
                        if (op == 0) { // snatch
                            auto amount = 1;
                            auto snatched = list.snatch_part_n_label(amount, thread_id);
                            if (!snatched.empty()) {
                                context.list_parts.push_back(snatched);
                            } else {
                                continue;
                            }
                        } else { // push
                            if (!context.list_parts.empty()) {
                                auto part = context.list_parts.back();
                                context.list_parts.pop_back();
                                list.push_n_label(std::move(part), num_threads);
                            } else {
                                continue;
                            }
                        }
                        ops++;
                    }

                    barrier.arrive_and_wait();

                    while (!context.list_parts.empty()) {
                        auto part = context.list_parts.back();
                        context.list_parts.pop_back();
                        list.push_n_label(std::move(part), num_threads);
                    }

                    barrier.arrive_and_wait();
                }
            });
        }
        dispatch(std::move(jobs));
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
    for (int i = 0; i < 16; i++) {
        std::cout << "test_atomic_list_mt " << i << " ..." << std::endl;
        atomic_list_tests::test_atomic_list_mt(50000, 1, 16, 200000);
    }

    std::cout << "test_atomic_push_snatch_list_st..." << std::endl;
    atomic_push_snatch_tests::test_atomic_push_snatch_list_st(1000);
    std::cout << "test_atomic_push_snatch_structure_st..." << std::endl;
    atomic_push_snatch_tests::test_atomic_push_snatch_structure_st();
    for (int i = 0; i < 16; i++) {
        std::cout << "test_atomic_push_snatch_list_mt " << i << " ..." << std::endl;
        atomic_push_snatch_tests::test_atomic_push_snatch_list_mt(100000, 16, 32, 200000);
    }

    std::cout << "done!" << std::endl;
    
    return 0;
}