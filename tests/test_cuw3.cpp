#include <condition_variable>
#include <mutex>
#include <vector>
#include <iostream>

#include <cuw3/cuw3.hpp>

#include "tests_common.hpp"


using namespace cuw3;

struct Alloc {
    explicit operator bool() const {
        return ptr;
    }

    void* ptr{};
    uint64 size{};
};

#define MAKE_AN_ABORTION(msg) do { std::cerr << msg << std::endl; std::abort(); } while (0)

uint64 estimated_allocation_size() {
    uint64 total_size{};
    for (uint64 alloc_size = 32; alloc_size <= (1 << 16); alloc_size += 32) {
        total_size += alloc_size;
    }
    for (uint64 alloc_size = 1 << 16; alloc_size <= (1 << 20); alloc_size += 1 << 15) {
        total_size += alloc_size;
    }
    for (uint64 alloc_size = 1 << 20; alloc_size <= (1 << 26); alloc_size += 1 << 20) {
        total_size += alloc_size;
    }
    return total_size;
}

void test_cuw3_st() {
    std::vector<Alloc> allocs{};
    for (uint64 alloc_size = 32; alloc_size <= (1 << 16); alloc_size += 32) {
        if (void* ptr = cuw3_alloc(alloc_size, 32)) {
            allocs.push_back({ptr, alloc_size});
        } else {
            MAKE_AN_ABORTION("failed to make an allocation");
        }
    }
    for (uint64 alloc_size = 1 << 16; alloc_size <= (1 << 20); alloc_size += 1 << 15) {
        if (void* ptr = cuw3_alloc(alloc_size, 32)) {
            allocs.push_back({ptr, alloc_size});
        } else {
            MAKE_AN_ABORTION("failed to make an allocation");
        }
    }
    for (uint64 alloc_size = 1 << 20; alloc_size <= (1 << 21); alloc_size += 1 << 20) {
        if (void* ptr = cuw3_alloc(alloc_size, 32)) {
            allocs.push_back({ptr, alloc_size});
        } else {
            MAKE_AN_ABORTION("failed to make an allocation");
        }
    }
    for (auto alloc : allocs) {
        cuw3_free(alloc.ptr, alloc.size);
    }
    allocs.clear();
}

void test_cuw3_st_mini() {
    std::vector<Alloc> allocs{};
    for (uint64 alloc_size = 32; alloc_size <= 1024; alloc_size += 32) {
        if (void* ptr = cuw3_alloc(alloc_size, 32)) {
            allocs.push_back({ptr, alloc_size});
        } else {
            MAKE_AN_ABORTION("failed to make an allocation");
        }
    }
    for (uint64 alloc_size = 1 << 16; alloc_size <= (1 << 18); alloc_size += 1 << 15) {
        if (void* ptr = cuw3_alloc(alloc_size, 32)) {
            allocs.push_back({ptr, alloc_size});
        } else {
            MAKE_AN_ABORTION("failed to make an allocation");
        }
    }
    for (uint64 alloc_size = 1 << 20; alloc_size <= (1 << 21); alloc_size += 1 << 20) {
        if (void* ptr = cuw3_alloc(alloc_size, 32)) {
            allocs.push_back({ptr, alloc_size});
        } else {
            MAKE_AN_ABORTION("failed to make an allocation");
        }
    }
    for (auto alloc : allocs) {
        cuw3_free(alloc.ptr, alloc.size);
    }
    allocs.clear();
}

void test_cuw3_mt_spam(uint total_spam_rounds, uint await_queue_size, uint alloc_rounds) {
    std::queue<std::thread> await_queue{};
    auto push_thread = [&] () {
        await_queue.push(std::thread([&](){
            for (uint round = 0; round < alloc_rounds; round++) {
                test_cuw3_st_mini();
            }
        }));
    };
    auto pop_thread = [&] () {
        await_queue.front().join();
        await_queue.pop();
    };

    for (uint i = 0; i < await_queue_size; i++) {
        push_thread();
    }
    for (uint spam_round = 0; spam_round < total_spam_rounds; spam_round++) {
        for (uint i = 0; i < await_queue_size; i++) {
            push_thread();
        }
        for (uint i = 0; i < await_queue_size; i++) {
            pop_thread();
        }
    }
    for (uint i = 0; i < await_queue_size; i++) {
        pop_thread();
    }
}

template<class T>
struct Channel {
    void push(T data) {
        auto lock_quard = std::lock_guard{lock};
        if (!closed) {
            queue.push(data);
            notify.notify_all();
        }
    }

    std::optional<T> pop() {
        auto lock_guard = std::unique_lock{lock};
        notify.wait(lock_guard, [&](){
            return closed || !queue.empty();
        });
        if (!queue.empty()) {
            auto popped = std::optional{std::move(queue.front())};
            queue.pop();
            return popped;
        }
        return std::nullopt;
    }

    void close() {
        auto lock_guard = std::lock_guard{lock};
        closed = true;
        notify.notify_all();
    }

    std::mutex lock{};
    std::condition_variable notify{};
    std::queue<Alloc> queue{};
    bool closed{};
};

// single consumer
template<class T>
struct MultiChannel {
    MultiChannel(uint num_channels) : channels(num_channels) {}

    void push(uint channel, T data) {
        channels[channel].push(data);
    }

    std::optional<T> pop() {
        for (uint i = 0; i < channels.size(); i++) {
            auto id = last_one++;
            if (auto item = channels[id % channels.size()].pop()) {
                return item;
            }
        }
        return std::nullopt;
    }

    void close(uint input_id) {
        channels[input_id].close();
    }

    std::vector<Channel<T>> channels{};
    uint last_one{};
};

struct ThreadContext {
    ThreadContext(uint num_channels) : input_channel(num_channels) {}

    MultiChannel<Alloc> input_channel;
};

struct ThreadContextStorage {
    ThreadContextStorage(uint num_threads) : thread_contexts(num_threads), barrier(num_threads) {
        for (uint thread = 0; thread < num_threads; thread++) {
            thread_contexts[thread] = std::make_unique<ThreadContext>(num_threads);
        }
    }

    ThreadContext& get_context(uint thread) {
        return *thread_contexts[thread];
    }

    uint get_num_contexts() const {
        return thread_contexts.size();
    }

    std::vector<std::unique_ptr<ThreadContext>> thread_contexts{};
    std::barrier<> barrier;
};

struct ThreadData {
    uint thread_id{};
    uint seed{};
    uint total_allocations{};
    uint max_alloc_size{};
};

void test_cuw3_mt_cross_worker(ThreadContextStorage& global_context, ThreadData thread_data) {
    std::minstd_rand gen(thread_data.seed);

    auto& this_thread_context = global_context.get_context(thread_data.thread_id);
    global_context.barrier.arrive_and_wait();

    for (uint allocation = 0; allocation < thread_data.total_allocations; allocation++) {
        uint64 size = gen() % thread_data.max_alloc_size;

        void* ptr = cuw3_alloc(size, 16);
        if (!ptr) {
            MAKE_AN_ABORTION("failed to make allocation");
        }

        uint other_thread_id = (thread_data.thread_id + allocation) % global_context.get_num_contexts();
        auto& other_thread_context = global_context.get_context(other_thread_id);
        other_thread_context.input_channel.push(thread_data.thread_id, {ptr, size});

    }
    for (uint thread_id = 0; thread_id < global_context.get_num_contexts(); thread_id++) {
        global_context.get_context(thread_id).input_channel.close(thread_data.thread_id);
    }
    while (auto alloc_opt = this_thread_context.input_channel.pop()) {
        cuw3_free(alloc_opt->ptr, alloc_opt->size);
    }
}

void test_cuw3_mt_cross(uint threads, uint total_allocations, uint max_alloc_size) {
    ThreadContextStorage global_context(threads);

    std::vector<std::thread> workers;
    for (uint thread_id = 0; thread_id < threads; thread_id++) {
        auto seed = thread_id;
        ThreadData thread_data = {thread_id, seed, total_allocations, max_alloc_size};
        workers.push_back(std::thread(test_cuw3_mt_cross_worker, std::ref(global_context), thread_data));
    }
    for (auto& worker : workers) {
        worker.join();
    }
}

void test_allocation_chaos(uint st, uint spam, uint cross) {
    uint total = st + spam + cross;

    std::barrier<> barrier(total);
    std::vector<std::thread> workers;
    for (uint i = 0; i < st; i++) {
        workers.push_back(
            std::thread(
                [&]() {
                    barrier.arrive_and_wait();
                    test_cuw3_st();
                }
            )
        );
    }
    for (uint i = 0; i < spam; i++) {
        workers.push_back(
            std::thread(
                [&]() {
                    barrier.arrive_and_wait();
                    test_cuw3_mt_spam(128, 8, 64);
                }
            )
        );
    }
    for (uint i = 0; i < cross; i++) {
        workers.push_back(
            std::thread(
                [&]() {
                    barrier.arrive_and_wait();
                    test_cuw3_mt_cross(4, 10000, 128);
                }
            )
        );
    }
    for (auto& worker : workers) {
        worker.join();
    }
}

int main() {
    std::cout << "estimated alloc size per thread(x4): " << estimated_allocation_size() * 4 << std::endl;

    std::cout << "test_cuw3_st" << std::endl;
    for (int i = 0; i < 1000; i++) {
        test_cuw3_st();
    }
    std::cout << "test_cuw3_st_mini" << std::endl;
    for (int i = 0; i < 1000; i++) {
        test_cuw3_st_mini();
    }
    std::cout << "test_cuw3_mt_spam" << std::endl;
    for (int i = 0; i < 10; i++) {
        test_cuw3_mt_spam(128, 8, 64);
    }
    std::cout << "test_cuw3_mt_cross" << std::endl;
    for (int i = 0; i < 16; i++) {
        test_cuw3_mt_cross(8, 10000, 128);
    }

    std::cout << "test_allocation_chaos(2,0,0)" << std::endl;
    test_allocation_chaos(2, 0, 0);
    std::cout << "test_allocation_chaos(0,2,0)" << std::endl;
    test_allocation_chaos(0, 2, 0);
    std::cout << "test_allocation_chaos(0,0,2)" << std::endl;
    test_allocation_chaos(0, 0, 2);
    std::cout << "test_allocation_chaos(4,4,4)" << std::endl;
    test_allocation_chaos(4, 4, 4);

    return 0;
}