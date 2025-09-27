#pragma once

#include <bit>
#include <array>
#include <algorithm>
#include <functional>
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
        return a & ~(~(U)0 >> b_log2);
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

    template<class T, Integer U>
    constexpr auto align(T* value, U alignment) {
        CUW3_ASSERT(is_alignment(alignment), "not alignment");
        return (T*)align((uintptr)value, alignment);
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


    inline ptrdiff subptr(const void* a, const void* b) {
        return (const char*)a - (const char*)b;
    }

    template<VoidLike T>
    inline T* advance_ptr(T* ptr, ptrdiff diff) {
        using Char = SameConstAs<T, char>;
        
        CUW3_ASSERT(ptr, "ptr must not be zero.");
        return (Char*)ptr + diff;
    }

    template<VoidLike T>
    inline T* advance_arr(T* ptr, intptr elem_size, intptr elem_index) {
        using Char = SameConstAs<T, char>;

        CUW3_ASSERT(ptr, "ptr must not be zero");
        return (Char*)ptr + elem_index * elem_size;
    }

    template<VoidLike T>
    inline T* advance_arr_log2(T* ptr, intptr elem_size_log2, intptr elem_index) {
        using Char = SameConstAs<T, char>;

        CUW3_ASSERT(ptr, "ptr must not be zero");
        return (Char*)ptr + mulpow2(elem_index, elem_size_log2);
    } 

    template<class To, class From>
    To* transform_ptr(From* from, ptrdiff diff) {
        CUW3_ASSERT(from, "from pointer must not be zero.");
        return (To*)advance_ptr(from, diff);
    }

    template<class Object, class Field>
    Object* field_to_obj(Field* field, ptrdiff offset) {
        CUW3_ASSERT(field, "field must not be zero.");
        return (Object*)advance_ptr(field, -offset);
    }

    #define cuw3_field_to_obj(field_ptr, Object, field_name) field_to_obj<Object>((field_ptr), (ptrdiff)offsetof(Object, field_name))


    template<class T, usize Size>
    constexpr usize array_size(const T (&array)[Size]) {
        return Size;
    }

    template<Integer T, usize Size>
    constexpr bool array_unique_ascending(const T (&array)[Size]) {
        for (usize i = 1; i < Size; i++) {
            if (array[i - 1] >= array[i]) {
                return false;
            }
        }
        return true;
    }

    template<Integer T, usize Size>
    constexpr bool all_sizes_valid(const T (&array)[Size]) {
        return std::all_of(std::begin(array), std::end(array), [] (auto& size) { return size <= 40; });
    }

    template<class It>
    constexpr bool all_equal(It first, It last) {
        return std::adjacent_find(first, last, std::not_equal_to{}) == last;
    }
    
    template<Integer T, usize Size>
    constexpr bool all_equal(const T (&array)[Size]) {
        return std::adjacent_find(std::begin(array), std::end(array), std::not_equal_to{}) == std::end(array);
    }
}