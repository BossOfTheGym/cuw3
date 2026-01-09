#pragma once

#include <algorithm>

#include "funcs.hpp"
#include "typedefs.hpp"

namespace cuw3 {
    template<UnsignedInteger T, gsize _bit_capacity>
    struct Bitmap {
        static constexpr T bit_capacity = _bit_capacity;
        static constexpr T bin_size = bitsize<T>();
        static constexpr T bin_capacity = (bit_capacity + bitsize<T>() - 1) / bitsize<T>();
        static constexpr T null_bit = bit_capacity;

        T _set_first_unset_bin(T bin, T mask) {
            T rel = std::countr_one(bins[bin] | mask);
            T bit = bin * bin_size + rel;
            bins[bin] = bitmask_set(bins[bin], rel);
            return bit;
        }

        T _set_first_unset_bin_range(T first_bit, T last_bit) {
            if (first_bit >= last_bit) {
                return null_bit;
            }
            T bin = first_bit / bin_size;
            T mask = bitmask_inv(first_bit % bin_size, last_bit % bin_size);
            if (!bitmask_all_set(bins[bin] | mask)) {
                return _set_first_unset_bin(bin, mask);
            }
            return null_bit;
        }

        T set_first_unset(T start = (T)0) {
            CUW3_ASSERT(start < bit_capacity, "invalid bit");

            T head_first_bit = start;
            T head_last_bit = std::min(align(start, bin_size), bit_capacity);
            T head_bit = _set_first_unset_bin_range(head_first_bit, head_last_bit);
            if (head_bit != null_bit) {
                return head_bit;
            }
            if (head_last_bit == bit_capacity) {
                return null_bit;
            }

            T first_bit = head_last_bit;
            T last_bit = bit_capacity - bit_capacity % bin_size;
            while (first_bit < last_bit) {
                T bin = first_bit / bin_size;
                if (!bitmask_all_set(bins[bin])) {
                    return _set_first_unset_bin(bin, 0);
                }
                first_bit += bin_size;
            }
            if (last_bit == bit_capacity) {
                return null_bit;
            }

            T tail_first_bit = last_bit;
            T tail_last_bit = bit_capacity;
            T tail_bit = _set_first_unset_bin_range(tail_first_bit, tail_last_bit);
            return tail_bit;
        }

        T _get_first_set_bin(T bin, T mask) const {
            T rel = std::countr_zero(bins[bin] & mask);
            T bit = bin * bin_size + rel;
            return bit;
        }

        T _get_first_set_bin_range(T first_bit, T last_bit) const {
            if (first_bit >= last_bit) {
                return null_bit;
            }
            T bin = first_bit / bin_size;
            T mask = bitmask(first_bit % bin_size, last_bit % bin_size);
            if (bitmask_any_set(bins[bin] & mask)) {
                return _get_first_set_bin(bin, mask);
            }
            return null_bit;
        }

        T get_first_set(T start = 0) const {
            CUW3_ASSERT(start < bit_capacity, "invalid bit");

            T head_first_bit = start;
            T head_last_bit = std::min(align(start, bin_size), bit_capacity);
            T head_bit = _get_first_set_bin_range(head_first_bit, head_last_bit);
            if (head_bit != null_bit) {
                return head_bit;
            }
            if (head_last_bit == bit_capacity) {
                return null_bit;
            }

            T first_bit = head_last_bit;
            T last_bit = bit_capacity - bit_capacity % bin_size;
            while (first_bit < last_bit) {
                T bin = first_bit / bin_size;
                if (bitmask_any_set(bins[bin])) {
                    return _get_first_set_bin(bin, bitmask_all<T>());
                }
                first_bit += bin_size;
            }
            if (last_bit == bit_capacity) {
                return null_bit;
            }

            T tail_first_bit = last_bit;
            T tail_last_bit = bit_capacity;
            T tail_bit = _get_first_set_bin_range(tail_first_bit, tail_last_bit);
            return tail_bit;
        }

        void set(T bit) {
            CUW3_ASSERT(bit < bit_capacity, "invalid bit");

            bins[bit / bin_size] |= (T)1 << (T)(bit % bin_size);
        }

        void unset(T bit) {
            CUW3_ASSERT(bit < bit_capacity, "invalid bit");

            bins[bit / bin_size] &= ~((T)1 << (T)(bit % bin_size));
        }

        void reset() {
            for (auto& bin : bins) {
                bin = 0;
            }
        }

        T bins[bin_capacity] = {};
    };
}