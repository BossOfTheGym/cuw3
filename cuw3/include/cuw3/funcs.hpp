#pragma once

#include <bit>
#include <type_traits>

#include "defs.hpp"
#include "assert.hpp"

namespace cuw3 {
    template<Integer T>
    constexpr auto as_unsigned(T value) {
        return (std::make_unsigned_t<T>)value;
    }

    template<Integer T>
    constexpr bool is_pow2(T value) {
        return std::has_single_bit(as_unsigned(value));
    }

    template<Integer T>
    constexpr T intlog2(T value) {
        if (value) {
            return std::bit_width(as_unsigned(value)) - 1;
        }
        return (T)0;
    }

    template<Integer T>
    constexpr T intpow2(T value) {
        return (T)1 << value;
    }

    template<Integer T>
    constexpr T nextpow2(T value) {
        if (is_pow2(value)) {
            return value;
        }
        return intpow2(std::bit_width(as_unsigned(value)));
    }

    template<Integer T>
    constexpr T pow2log2(T value) {
        return (T)std::countr_zero(as_unsigned(value));
    }

    template<Integer T, Integer U>
    constexpr auto mulpow2(T a, U b_log2) {
        return a << b_log2;
    }

    template<Integer T, Integer U>
    constexpr auto divpow2(T a, U b_log2) {
        return a >> b_log2;
    }

    template<Integer T, Integer U>
    constexpr auto modpow2(T a, U b_log2) {
        return a & (((U)1 << b_log2) - 1);
    }

    template<Integer T>
    constexpr bool is_alignment(T value) {
        return std::has_single_bit(as_unsigned(value));
    }

    template<Integer T, Integer U>
    constexpr bool is_aligned(T value, U alignment) {
        CUW3_ASSERT(is_alignment(alignment), "not alignment");
        return !(value & (alignment - 1));
    }

    template<class T, Integer U>
    constexpr bool is_aligned(T* ptr, U alignment) {
        CUW3_ASSERT(is_alignment(alignment), "not_alignment");
        return is_aligned((uintptr)ptr, alignment);
    }

    template<class T, class U>
    constexpr bool is_type_aligned(U* ptr) {
        return is_aligned(ptr, alignof(T));
    }

    template<Integer T, Integer U>
    constexpr auto align(T value, U alignment) {
        CUW3_ASSERT(is_alignment(alignment), "not alignment");
        return (value + alignment - 1) & -alignment;
    }

    template<Integer T>
    constexpr T bitsize() {
        return sizeof(T) * (T)8;
    }

    template<Integer T, Integer U>
    constexpr auto mulchunk(T a, U b, U b_log2 = (U)0) -> decltype(a * b) {
        if (b_log2) {
            return mulpow2(a, b_log2);
        }
        return a * b;
    }

    template<Integer T, Integer U>
    constexpr auto divchunk(T a, U b, U b_log2 = (U)0) -> decltype(a / b) {
        if (b_log2) {
            return divpow2(a, b_log2);
        }
        return a / b;
    }
}