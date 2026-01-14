#include "cuw3/atomic.hpp"

#include <vector>
#include <thread>
#include <future>
#include <utility>
#include <iostream>
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
    for (auto result :
 results) {
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

namespace atomic_list_tests {
    using ListLinkType = uint64;

    struct ListDataNode {
        ListLinkType next{};
        uint64 data{};
    };

    struct ListTraits {
        using LinkType = ListLinkType;

        struct ListHead {
            LinkType version : 32{}, next : 32{};
        };

        static constexpr LinkType null_link = 0xFFFFFFFF;
        static constexpr LinkType op_failed = 0xFFFFFFFE;
    };

    struct ListNodeOps {
        void set_next(ListLinkType node, ListLinkType next) {

        }

        ListLinkType get_next(ListLinkType node) {

        }

        ListDataNode* nodes{};
        uint64 num_nodes{};
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
    return 0;
}