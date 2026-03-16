#pragma once

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


#include "cuw3/vmem.hpp"
#include "cuw3/funcs.hpp"

using namespace cuw3;


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

inline auto get_job_part(uint start, uint stop, uint parts, uint part_id) -> JobPart {
    uint delta = stop - start + parts - 1;
    delta -= delta % parts;
    uint part = delta / parts;
    uint job_start = std::clamp(part * part_id, start, stop);
    uint job_stop = std::clamp(job_start + part, start, stop);
    return {job_start, job_stop};
}

template<class T>
void shuffle(std::vector<T>& vec) {
    if (vec.size() == 0) {
        return;
    }

    std::minstd_rand rand{std::random_device{}()};
    for (uint i = 1; i < vec.size(); i++) {
        std::swap(vec[rand() % i], vec.back());
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