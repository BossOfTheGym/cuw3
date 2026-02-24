#pragma once

#include <algorithm>

#include "cuw3/assert.hpp"
#include "funcs.hpp"
#include "typedefs.hpp"

namespace cuw3 {
    template<UnsignedInteger T, gsize _bit_capacity>
    struct Bitmap {
        static constexpr T bit_capacity = _bit_capacity;
        static constexpr T bin_size = bitsize<T>();
        static constexpr T bin_capacity = (bit_capacity + bitsize<T>() - 1) / bitsize<T>();
        static constexpr T null_bit = bit_capacity;

        gsize _set_first_unset_bin(T bin, T mask) {
            gsize rel = std::countr_one(bins[bin] | mask);
            gsize bit = bin * bin_size + rel;
            bins[bin] = bitmask_set(bins[bin], rel);
            return bit;
        }

        gsize _set_first_unset_bin_range(T first_bit, T last_bit) {
            if (first_bit >= last_bit) {
                return null_bit;
            }
            gsize bin = first_bit / bin_size;
            gsize mask = bitmask_inv(first_bit % bin_size, last_bit % bin_size);
            if (!bitmask_all_set(bins[bin] | mask)) {
                return _set_first_unset_bin(bin, mask);
            }
            return null_bit;
        }

        gsize _get_first_set_bin(T bin, T mask) const {
            gsize rel = std::countr_zero(bins[bin] & mask);
            gsize bit = bin * bin_size + rel;
            return bit;
        }

        gsize _get_first_set_bin_range(T first_bit, T last_bit) const {
            if (first_bit >= last_bit) {
                return null_bit;
            }
            gsize bin = first_bit / bin_size;
            gsize mask = bitmask(first_bit % bin_size, last_bit % bin_size);
            if (bitmask_any_set(bins[bin] & mask)) {
                return _get_first_set_bin(bin, mask);
            }
            return null_bit;
        }


        gsize set_first_unset(gsize start = 0) {
            CUW3_ASSERT(start < bit_capacity, "invalid bit");

            gsize head_first_bit = start;
            gsize head_last_bit = std::min(align(start, bin_size), bit_capacity);
            gsize head_bit = _set_first_unset_bin_range(head_first_bit, head_last_bit);
            if (head_bit != null_bit) {
                return head_bit;
            }
            if (head_last_bit == bit_capacity) {
                return null_bit;
            }

            gsize first_bit = head_last_bit;
            gsize last_bit = bit_capacity - bit_capacity % bin_size;
            while (first_bit < last_bit) {
                gsize bin = first_bit / bin_size;
                if (!bitmask_all_set(bins[bin])) {
                    return _set_first_unset_bin(bin, 0);
                }
                first_bit += bin_size;
            }
            if (last_bit == bit_capacity) {
                return null_bit;
            }

            gsize tail_first_bit = last_bit;
            gsize tail_last_bit = bit_capacity;
            gsize tail_bit = _set_first_unset_bin_range(tail_first_bit, tail_last_bit);
            return tail_bit;
        }

        gsize get_first_set(gsize start = 0) const {
            CUW3_ASSERT(start < bit_capacity, "invalid bit");

            gsize head_first_bit = start;
            gsize head_last_bit = std::min(align(start, bin_size), bit_capacity);
            gsize head_bit = _get_first_set_bin_range(head_first_bit, head_last_bit);
            if (head_bit != null_bit) {
                return head_bit;
            }
            if (head_last_bit == bit_capacity) {
                return null_bit;
            }

            gsize first_bit = head_last_bit;
            gsize last_bit = bit_capacity - bit_capacity % bin_size;
            while (first_bit < last_bit) {
                gsize bin = first_bit / bin_size;
                if (bitmask_any_set(bins[bin])) {
                    return _get_first_set_bin(bin, bitmask_all<T>());
                }
                first_bit += bin_size;
            }
            if (last_bit == bit_capacity) {
                return null_bit;
            }

            gsize tail_first_bit = last_bit;
            gsize tail_last_bit = bit_capacity;
            gsize tail_bit = _get_first_set_bin_range(tail_first_bit, tail_last_bit);
            return tail_bit;
        }

        void set(gsize bit) {
            CUW3_ASSERT(bit < bit_capacity, "invalid bit");

            bins[bit / bin_size] |= (T)1 << (T)(bit % bin_size);
        }

        void unset(gsize bit) {
            CUW3_ASSERT(bit < bit_capacity, "invalid bit");

            bins[bit / bin_size] &= ~((T)1 << (T)(bit % bin_size));
        }

        void reset() {
            for (auto& bin : bins) {
                bin = 0;
            }
        }


        bool get(gsize bit) const {
            CUW3_ASSERT(bit < bit_capacity, "invalid bit");

            return bins[bit / bin_size] >> (bit % bin_size) & 1;
        }

        bool any_set(gsize start = 0) const {
            CUW3_ASSERT(start < bit_capacity, "invlaid bit");

            gsize curr = start;
            gsize last = bit_capacity;
            while (curr < last) {
                gsize next = std::min(align_down(curr + bin_size, bin_size), last);
                gsize mask = bitmask(curr % bin_size, curr % bin_size + next - curr);
                if (bins[curr / bin_size] & mask) {
                    return true;
                }
            }
            return false;
        }

        bool all_reset(gsize start = 0) const {
            return !any_set(start);
        }


        gsize count(gsize start = 0) const {
            CUW3_ASSERT(start < bit_capacity, "invalid bit");

            gsize total = 0;
            gsize curr = start;
            gsize last = bit_capacity;
            while (curr < last) {
                gsize next = std::min(align_down(curr + bin_size, bin_size), last);
                gsize mask = bitmask(curr % bin_size, curr % bin_size + next - curr);
                total += std::popcount(bins[curr / bin_size] & mask);
                curr = next;
            }
            return total;
        }

        gsize sample_set_bit(gsize seed, gsize start = 0) const {
            gsize total = count(start);
            if (total == 0) {
                return null_bit;
            }

            gsize skipped = 0;
            gsize bit = seed % total;
            gsize curr = start;
            gsize last = bit_capacity;
            while (curr < last) {
                gsize next = std::min(align_down(curr + bin_size, bin_size), last);
                gsize mask = bitmask(curr % bin_size, curr % bin_size + next - curr);
                gsize masked = bins[curr / bin_size] & mask;
                gsize bits_in_range = std::popcount(masked);
                if (bit - skipped < bits_in_range) {
                    gsize corrected = masked;
                    for (gsize i = 0; i < bit - skipped; i++) {
                        corrected &= corrected - 1;
                    }
                    gsize result = curr + std::countr_zero(corrected);
                    return result;
                }
                skipped += bits_in_range;
                curr = next;
            }

            // we are always guaranteed to have enough set bits in the set
            // must never happen
            CUW3_ABORT("unreachable");
        }

        T bins[bin_capacity] = {};
    };
}