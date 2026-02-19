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