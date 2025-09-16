#pragma once

#include <immintrin.h>

#include <algorithm>

namespace cuw3 {
    inline void stall_execution() {
        _mm_pause();
    }

    struct SimpleBackoff {
        void operator() () {
            stall_execution();
        }
    };

    template<int spins>
    struct ConstantBackoff {
        void operator() () {
            for (int i = 0; i < spins; i++) {
                stall_execution();
            }
        }
    };

    template<int a, int b, int max_spins>
    struct ExpBackoff {
        void operator() () {
            for (int i = 0; i < spins; i++) {
                stall_execution();
            }
            spins = std::min(a * spins + b, max_spins);
        }

        int spins{};
    };
}