#include <cuw3/cuw3.hpp>

#include <benchmark/benchmark.h>

#include <format>
#include <random>
#include <vector>
#include <variant>
#include <iterator>

using namespace cuw3;


struct AllocationRequest {
    uint64 size{};
    uint64 alignment{};
};

struct DeallocationRequest {
    uint id{};
};

using Request = std::variant<AllocationRequest, DeallocationRequest>;
using RequestList = std::vector<Request>;


struct Allocation {
    void* ptr{};
    uint64 size{};
};

struct RequestContext {
    bool any_alive_allocs() const {
        for (auto& alloc : allocations) {
            if (alloc.ptr) {
                return true;
            }
        }
        return false;
    }

    void clear() {
        allocations.clear();
    }

    std::vector<Allocation> allocations{};
};


struct Cuw3Allocator {
    void* allocate(uint64 size, uint64 alignment) const {
        return cuw3_alloc(size, alignment);
    }

    void deallocate(void* ptr, uint64 size) const {
        cuw3_free(ptr, size);
    }
};

struct StdAllocator {
    void* allocate(uint64 size, uint64 alignment) const {
        (void)alignment;
        return malloc(size);
    }

    void deallocate(void* ptr, uint64 size) const {
        (void)size;
        free(ptr);
    }
};

struct RequestExecutor {
    template<class Allocator>
    bool exec_list(const Allocator& allocator, RequestContext& context, const RequestList& reqs) const {
        for (auto& req : reqs) {
            if (!std::visit([&] (auto& r) { return exec(allocator, context, r); }, req)) {
                return false;
            }
        }
        return true;
    }

    template<class Allocator>
    bool exec(const Allocator& allocator, RequestContext& context, const AllocationRequest& req) const {
        if (auto* ptr = allocator.allocate(req.size, req.alignment)) {
            context.allocations.push_back({ptr, req.size});
            return true;
        }
        return false;
    }

    template<class Allocator>
    bool exec(const Allocator& allocator, RequestContext& context, const DeallocationRequest& req) const {
        auto alloc = context.allocations[req.id];
        if (alloc.ptr) {
            allocator.deallocate(alloc.ptr, alloc.size);
            context.allocations[req.id] = {};
            return true;
        }
        return false;
    }
};


RequestList create_req_list_alloc_dealloc(uint64 seed, uint64 low, uint64 high, uint64 alignment, uint64 count) {
    RequestList reqs{};
    std::minstd_rand rand(seed ? seed : std::random_device{}());
    std::uniform_int_distribution<uint64> dist(low, high);

    reqs.reserve(2 * count);
    for (uint i = 0; i < count; i++) {
        reqs.push_back(AllocationRequest{dist(rand), alignment});
    }
    for (uint i = 0; i < count; i++) {
        reqs.push_back(DeallocationRequest{i});
    }
    return reqs;
}


template<class It, class Gen>
void sample(It first, It last, Gen& gen) {
    auto d = std::distance(first, last);
    auto sampled = first;
    std::advance(sampled, gen() % d);
    std::iter_swap(sampled, std::prev(last));
}

RequestList create_req_list_alloc_dealloc_chaos(uint64 seed, uint64 low, uint64 high, uint64 alignment, uint64 count) {
    RequestList reqs{};
    std::minstd_rand gen(seed ? seed : std::random_device{}());
    std::uniform_int_distribution<uint64> dist(low, high);

    uint reqs_created = 0;
    uint alloc_reqs = 0;
    std::vector<uint> alive_allocations{};

    auto create_alloc_req = [&] () {
        uint alloc_id = alloc_reqs++;
        uint64 alloc_size = dist(gen);
        alive_allocations.push_back(alloc_id);
        reqs.push_back(AllocationRequest{alloc_size, alignment});
        reqs_created++;
    };

    auto create_dealloc_req = [&] () {
        sample(alive_allocations.begin(), alive_allocations.end(), gen);
        reqs.push_back(DeallocationRequest{alive_allocations.back()});
        alive_allocations.pop_back();
        reqs_created++;
    };

    while (reqs_created < 2 * count) {
        auto alloc_req = gen() % 2 == 0;
        if (alloc_req) {
            if (alloc_reqs < count) {
                create_alloc_req();
            } else {
                create_dealloc_req();
            }
        } else {
            if (!alive_allocations.empty()) {
                create_dealloc_req();
            } else {
                create_alloc_req();
            }
        }
    }
    return reqs;
}

template<class Allocator>
void execute_benchmark(benchmark::State& state, const Allocator& alloc, const RequestExecutor& executor, RequestContext& context, const RequestList& reqs, const char* name) {
    for (const auto& _ : state) {
        if (!executor.exec_list(alloc, context, reqs)) {
            state.SkipWithError(std::format("{}: failed to finish request", name));
            return;
        }
        if (context.any_alive_allocs()) {
            state.SkipWithError(std::format("{}: context was not empty", name));
            return;
        }
        context.clear();
    }
}


// all consts are taken from the cuw3.cpp file
void cuw3_bench_small_alloc_dealloc(benchmark::State& state) {
    RequestExecutor executor{};
    RequestContext context{};
    RequestList reqs = create_req_list_alloc_dealloc(42 + state.thread_index(), 16, 16384, 16, 1 << 18);
    execute_benchmark(state, Cuw3Allocator{}, executor, context, reqs, "bench_small_alloc_dealloc");
}

void cuw3_bench_small_alloc_dealloc_chaos(benchmark::State& state) {
    RequestExecutor executor{};
    RequestContext context{};
    RequestList reqs = create_req_list_alloc_dealloc_chaos(42 + state.thread_index(), 16, 16384, 16, 1 << 18);
    execute_benchmark(state, Cuw3Allocator{}, executor, context, reqs, "bench_small_alloc_dealloc_chaos");
}

void cuw3_bench_small_alloc_dealloc_chaos_mt(benchmark::State& state) {
    cuw3_bench_small_alloc_dealloc_chaos(state);
}

void cuw3_bench_medium_alloc_dealloc(benchmark::State& state) {
    RequestExecutor executor{};
    RequestContext context{};
    RequestList reqs = create_req_list_alloc_dealloc(42 + state.thread_index(), 16384, 1 << 18, 16, 1 << 15);
    execute_benchmark(state, Cuw3Allocator{}, executor, context, reqs, "bench_medium_alloc_dealloc");
}

void cuw3_bench_medium_alloc_dealloc_chaos(benchmark::State& state) {
    RequestExecutor executor{};
    RequestContext context{};
    RequestList reqs = create_req_list_alloc_dealloc_chaos(42 + state.thread_index(), 16384, 1 << 18, 16, 1 << 15);
    execute_benchmark(state, Cuw3Allocator{}, executor, context, reqs, "bench_medium_alloc_dealloc_chaos");
}

void cuw3_bench_medium_alloc_dealloc_chaos_mt(benchmark::State& state) {
    cuw3_bench_medium_alloc_dealloc_chaos(state);
}

void cuw3_bench_mixed_alloc_dealloc(benchmark::State& state) {
    RequestExecutor executor{};
    RequestContext context{};
    RequestList reqs = create_req_list_alloc_dealloc(42 + state.thread_index(), 16, 1 << 18, 16, 1 << 15);
    execute_benchmark(state, Cuw3Allocator{}, executor, context, reqs, "bench_mixed_alloc_dealloc");
}

void cuw3_bench_mixed_alloc_dealloc_chaos(benchmark::State& state) {
    RequestExecutor executor{};
    RequestContext context{};
    RequestList reqs = create_req_list_alloc_dealloc_chaos(42 + state.thread_index(), 16, 1 << 18, 16, 1 << 15);
    execute_benchmark(state, Cuw3Allocator{}, executor, context, reqs, "bench_mixed_alloc_dealloc_chaos");
}

void cuw3_bench_mixed_alloc_dealloc_chaos_mt(benchmark::State& state) {
    cuw3_bench_mixed_alloc_dealloc_chaos(state);
}


// all consts are taken from the cuw3.cpp file
void std_bench_small_alloc_dealloc(benchmark::State& state) {
    RequestExecutor executor{};
    RequestContext context{};
    RequestList reqs = create_req_list_alloc_dealloc(42 + state.thread_index(), 16, 16384, 16, 1 << 18);
    execute_benchmark(state, StdAllocator{}, executor, context, reqs, "bench_small_alloc_dealloc");
}

void std_bench_small_alloc_dealloc_chaos(benchmark::State& state) {
    RequestExecutor executor{};
    RequestContext context{};
    RequestList reqs = create_req_list_alloc_dealloc_chaos(42 + state.thread_index(), 16, 16384, 16, 1 << 18);
    execute_benchmark(state, StdAllocator{}, executor, context, reqs, "bench_small_alloc_dealloc_chaos");
}

void std_bench_small_alloc_dealloc_chaos_mt(benchmark::State& state) {
    std_bench_small_alloc_dealloc_chaos(state);
}

void std_bench_medium_alloc_dealloc(benchmark::State& state) {
    RequestExecutor executor{};
    RequestContext context{};
    RequestList reqs = create_req_list_alloc_dealloc(42 + state.thread_index(), 16384, 1 << 18, 16, 1 << 15);
    execute_benchmark(state, StdAllocator{}, executor, context, reqs, "bench_medium_alloc_dealloc");
}

void std_bench_medium_alloc_dealloc_chaos(benchmark::State& state) {
    RequestExecutor executor{};
    RequestContext context{};
    RequestList reqs = create_req_list_alloc_dealloc_chaos(42 + state.thread_index(), 16384, 1 << 18, 16, 1 << 15);
    execute_benchmark(state, StdAllocator{}, executor, context, reqs, "bench_medium_alloc_dealloc_chaos");
}

void std_bench_medium_alloc_dealloc_chaos_mt(benchmark::State& state) {
    std_bench_medium_alloc_dealloc_chaos(state);
}

void std_bench_mixed_alloc_dealloc(benchmark::State& state) {
    RequestExecutor executor{};
    RequestContext context{};
    RequestList reqs = create_req_list_alloc_dealloc(42 + state.thread_index(), 16, 1 << 18, 16, 1 << 15);
    execute_benchmark(state, StdAllocator{}, executor, context, reqs, "bench_mixed_alloc_dealloc");
}

void std_bench_mixed_alloc_dealloc_chaos(benchmark::State& state) {
    RequestExecutor executor{};
    RequestContext context{};
    RequestList reqs = create_req_list_alloc_dealloc_chaos(42 + state.thread_index(), 16, 1 << 18, 16, 1 << 15);
    execute_benchmark(state, StdAllocator{}, executor, context, reqs, "bench_mixed_alloc_dealloc_chaos");
}

void std_bench_mixed_alloc_dealloc_chaos_mt(benchmark::State& state) {
    std_bench_mixed_alloc_dealloc_chaos(state);
}


BENCHMARK(cuw3_bench_small_alloc_dealloc);
BENCHMARK(std_bench_small_alloc_dealloc);

BENCHMARK(cuw3_bench_small_alloc_dealloc_chaos);
BENCHMARK(std_bench_small_alloc_dealloc_chaos);

BENCHMARK(cuw3_bench_small_alloc_dealloc_chaos_mt)->Threads(12);
BENCHMARK(std_bench_small_alloc_dealloc_chaos_mt)->Threads(12);


BENCHMARK(cuw3_bench_medium_alloc_dealloc);
BENCHMARK(std_bench_medium_alloc_dealloc);

BENCHMARK(cuw3_bench_medium_alloc_dealloc_chaos);
BENCHMARK(std_bench_medium_alloc_dealloc_chaos);

BENCHMARK(cuw3_bench_medium_alloc_dealloc_chaos_mt)->Threads(12);
BENCHMARK(std_bench_medium_alloc_dealloc_chaos_mt)->Threads(12);


BENCHMARK(cuw3_bench_mixed_alloc_dealloc);
BENCHMARK(std_bench_mixed_alloc_dealloc);

BENCHMARK(cuw3_bench_mixed_alloc_dealloc_chaos);
BENCHMARK(std_bench_mixed_alloc_dealloc_chaos);

BENCHMARK(cuw3_bench_mixed_alloc_dealloc_chaos_mt)->Threads(12);
BENCHMARK(std_bench_mixed_alloc_dealloc_chaos_mt)->Threads(12);



BENCHMARK_MAIN();