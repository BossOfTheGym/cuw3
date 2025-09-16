#pragma once

#include "defs.hpp"
#include "funcs.hpp"
#include "assert.hpp"

namespace cuw3 {
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
            _data = value | alignment;
        }

        T value() const noexcept {
            return _data & value_mask;
        }

        T alignment() const noexcept {
            return _data & alignment_mask;
        }

        T raw() const {
            return _data;
        }

        T _data{};
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

        template<class Type = void>
        Type* ptr() const noexcept {
            return (Type*)Base::value();
        }

        T data() const {
            return Base::alignment();
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