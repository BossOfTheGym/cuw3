#include "cuw3/thread_graveyard.hpp"

#include "tests_common.hpp"

using namespace cuw3;


using ThreadGraveyardOps = DefaultThreadGraveyardOps;

struct TestThreadGraveyardElement {
    [[nodiscard]] static TestThreadGraveyardElement* from_entry(void* entry) {
        return cuw3_field_to_obj(entry, TestThreadGraveyardElement, entry);
    }

    void mark_acquired() {
        auto ref = std::atomic_ref{status};
        auto old = ref.exchange(1, std::memory_order_relaxed);
        CUW3_CHECK(old == 0, "WTF");
    }

    void mark_released() {
        auto ref = std::atomic_ref{status};
        auto old = ref.exchange(0, std::memory_order_relaxed);
        CUW3_CHECK(old == 1, "WTF");
    }

    bool acquire(uint64 limit) {
        acquired++;
        return acquired >= limit;
    }

    void force_acquire(uint64 limit) {
        acquired = limit;
    }

    DefaultThreadGraveyardEntry entry{};
    uint64 status{};
    uint64 acquired{};
};

struct TestThreadGraveyard {
    TestThreadGraveyard(uint64 _num_grave_entries, uint64 _num_elements) {
        auto* graveyard_check = ThreadGraveyard::create(Memory::from(&graveyard), _num_grave_entries);
        CUW3_CHECK(graveyard_check, "failed to initialize graveyard");

        num_elements = _num_elements;
        elements = std::make_unique<TestThreadGraveyardElement[]>(num_elements);
        CUW3_CHECK(elements, "failed to create elements");

        for (uint i = 0; i < num_elements; i++) {
            put_thread(&elements[i]);
        }
    }

    // TODO :  bad design
    [[nodiscard]] ThreadGraveData acquire(uint start = 0, uint rounds = 4) {
        ThreadGraveAcquireParams params{};
        params.rounds = rounds;
        params.start = start;
        auto acquired = graveyard.acquire(ThreadGraveyardOps{}, params);
        if (acquired) {
            auto* entry = TestThreadGraveyardElement::from_entry(acquired.data);
            entry->mark_acquired();
        }
        return acquired;
    }

    // CHANGE: release_thread -> remove_thread
    void remove_thread(ThreadGraveData grave_data) {
        auto* entry = TestThreadGraveyardElement::from_entry(grave_data.data);
        entry->mark_released();
        graveyard.empty_grave(grave_data);
    }

    // CHANGE:put_thread_back -> release_thread
    void release_thread(ThreadGraveData grave_data) {
        auto* entry = TestThreadGraveyardElement::from_entry(grave_data.data);
        entry->mark_released();
        graveyard.release_thread(grave_data, ThreadGraveyardOps{});
    }

    // CHANGE:put_thread_to_rest -> put_thread_to_rest
    void put_thread(TestThreadGraveyardElement* elem) {
        graveyard.put_thread(&elem->entry, ThreadGraveyardOps{});
    }

    uint64 get_num_elements() const {
        return num_elements;
    }

    uint64 get_num_grave_entries() const {
        return graveyard.num_grave_entries;
    }

    bool is_empty() {
        return graveyard.is_empty();
    }

    ThreadGraveyard graveyard{};
    std::unique_ptr<TestThreadGraveyardElement[]> elements{};
    uint64 num_elements{};
};


void test_graveyard_st(uint rounds) {
    constexpr uint num_grave_entries = 8;

    TestThreadGraveyard graveyard(num_grave_entries, num_grave_entries * 2);

    std::vector<ThreadGraveData> acquired{};
    for (uint round = 0; round < rounds; round++) {
        while (true) {
            auto grave = graveyard.acquire();
            if (!grave) {
                break;
            }
            acquired.push_back(grave);
        }

        CUW3_CHECK(acquired.size() == graveyard.get_num_elements(), "failed to acquire all entries");

        for (auto grave : acquired) {
            graveyard.release_thread(grave);
        }
        acquired.clear();
    }

    for (uint i = 0; i < graveyard.get_num_elements(); i++) {
        auto grave = graveyard.acquire();
        CUW3_CHECK(grave, "there must have been a grave");
        graveyard.remove_thread(grave);
    }
    CUW3_CHECK(graveyard.is_empty(), "graveyard must have been empty");
}

void test_graveyard_chaos_mt(uint num_elements, uint num_threads, uint acquire_stop) {
    constexpr uint num_grave_entries = 16;

    TestThreadGraveyard graveyard(num_grave_entries, num_elements);

    struct alignas(conf_cacheline) ThreadContext {
        uint last_grave{};
    };

    std::barrier initial_sync{num_threads};
    std::vector<ThreadContext> thread_contexts{num_threads};

    std::vector<std::thread> workers{num_threads};
    for (uint thread_id = 0; thread_id < num_threads; thread_id++) {
        workers[thread_id] = std::thread([&, thread_id] () {
            initial_sync.arrive_and_wait();

            auto& thread_context = thread_contexts[thread_id];
            while (true) {
                auto acquired = graveyard.acquire(thread_context.last_grave, 1024);
                if (!acquired) {
                    break;
                }
                thread_context.last_grave = acquired.grave;

                auto* element = TestThreadGraveyardElement::from_entry(acquired.data);
                if (element->acquire(acquire_stop)) {
                    graveyard.release_thread(acquired);
                } else {
                    graveyard.remove_thread(acquired);
                }
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    if (graveyard.is_empty()) {
        return;
    }

    std::cout << "very unlikely event but graveyard was not empty after chaos events. cleaning up...\n";
    
    while (true) {
        auto acquired = graveyard.acquire();
        if (!acquired) {
            break;
        }

        auto* element = TestThreadGraveyardElement::from_entry(acquired.data);
        element->force_acquire(acquire_stop);
        graveyard.release_thread(acquired);
    }
    if (!graveyard.is_empty()) {
        for (uint i = 0; i < graveyard.num_elements; i++) {
            auto& elem = graveyard.elements[i];
            std:: cout << i << " " << elem.status << " " << elem.acquired << "\n";
        }
    }
    CUW3_CHECK(graveyard.is_empty(), "graveyard must be empty at that point");
}

int main() {
    test_graveyard_st(16);

    std::cout << "test_graveyard_chaos_mt(128, 2, 1 << 12)" << std::endl;
    test_graveyard_chaos_mt(128, 2, 1 << 12);
    std::cout << "test_graveyard_chaos_mt(8, 8, 1 << 17)" << std::endl;
    test_graveyard_chaos_mt(8, 8, 1 << 17);
    std::cout << "test_graveyard_chaos_mt(128, 8, 1 << 12)" << std::endl;
    test_graveyard_chaos_mt(128, 8, 1 << 12);

    std::cout << "it's done!" << std::endl;

    return 0;
}