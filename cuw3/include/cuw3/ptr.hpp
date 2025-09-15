#pragma once

#include "defs.hpp"
#include "funcs.hpp"
#include "assert.hpp"

namespace cuw3 {
    inline void* advance_ptr(void* ptr, ptrdiff diff) {
        CUW3_ASSERT(ptr, "ptr must not be zero.");
        return (char*)ptr + diff;
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

    template<Integer T, T bits>
    struct AlignmentPackedInt {
        static_assert(bits > 0 && bits < bitsize<T>());

        static constexpr T alignment_mask = ((T)1 << bits) - 1;
        static constexpr T value_mask = ~alignment_mask;

        static AlignmentPackedInt packed(T value, T alignment) {
            AlignmentPackedInt result{};
            result.pack(value, alignment);
            return result;
        }
        
        void pack(T value, T alignment) {
            CUW3_ASSERT(!(value & ~value_mask), "bad value: garbage in alignment bits");
            CUW3_ASSERT(!(alignment & ~alignment_mask), "bad alignment: garbage in value bits");
            data = value | alignment;
        }

        T value() const noexcept {
            return data & value_mask;
        }

        T alignment() const noexcept {
            return data & alignment_mask;
        }

        T data{};
    };

    template<IntptrLike T, T bits>
    struct AlignmentPackedPtr : AlignmentPackedInt<T, bits> {
        using Base = AlignmentPackedInt<T, bits>;

        static AlignmentPackedPtr packed(void* ptr, T alignment) {
            return {Base::packed((T)ptr, alignment)};
        }

        void pack(void* ptr, T alignment) noexcept {
            Base::pack((T)ptr, alignment);
        }

        template<class Type>
        Type* ptr() const noexcept {
            return (Type*)Base::value();
        }
    };

    template<IntptrLike T>
    struct OffsetPtr {
        static OffsetPtr packed(void* ptr, T value, T alignment) {
            CUW3_ASSERT(is_alignment(alignment), "not alignment");
            CUW3_ASSERT((value & ~(alignment - 1)) == 0, "bad value: garbage in ptr bits");
            CUW3_ASSERT(((T)ptr & (alignment - 1)) == 0, "bad ptr: garbage in alignment bits");
            return OffsetPtr{(T)ptr | value};
        }

        template<class Type>
        Type* ptr(T alignment) const {
            CUW3_ASSERT(is_alignment(alignment), "not alignment");
            return (Type*)(value & ~(alignment - 1));
        }

        ptrdiff offset(T alignment) const {
            CUW3_ASSERT(is_alignment(alignment), "not alignment");
            return value & (alignment - 1);
        }

        T value{};
    };

    struct NullOffsetPtr {
        template<class T>
        operator OffsetPtr<T>() const {
            return {};
        }
    };

    inline constexpr NullOffsetPtr null_offset_ptr = {};
}