#pragma once

#include <algorithm>

#include "funcs.hpp"
#include "typedefs.hpp"

namespace cuw3 {
    template<UnsignedInteger T, gsize bit_capacity>
    struct Bitmap {
        static constexpr gsize bin_capacity = (bit_capacity + bitsize<T>() - 1) / bitsize<T>();
        static constexpr gsize null_bit = bit_capacity;

        gsize set_first_unset(gsize start = (gsize)0) {
            for (gsize i = start / bitsize<T>(); i < bin_capacity; i++) {
                if (bins[i] == ~(T)0) {
                    continue;
                }

                gsize rel = std::countr_one(bins[i]);
                gsize base = i * bitsize<T>();
                gsize bit = base + rel;
                if (bit >= bit_capacity) {
                    return null_bit;
                }
                bins[i] |= (T)1 << (T)rel;
                return bit;
            }
            return null_bit;
        }

        void set(gsize bit) {
            CUW3_ASSERT(bit < bit_capacity, "invalid bit");

            bins[bit / bitsize<T>()] |= (T)1 << (T)(bit % bitsize<T>());
        }

        void unset(gsize bit) {
            CUW3_ASSERT(bit < bit_capacity, "invalid bit");

            bins[bit / bitsize<T>()] &= ~((T)1 << (T)(bit % bitsize<T>()));
        }

        T bins[bin_capacity] = {};
    };
}