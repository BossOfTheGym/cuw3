#pragma once

#include <cstdint>
#include <cstddef>
#include <concepts>
#include <type_traits>

// configured
#include <cuw3/config.hpp>

#define CUW3_CACHELINE_SIZE 64
#define CUW3_CONTROL_BLOCK_SIZE 128
#define CUW3_PAGE_SIZE 4096
#define CUW3_HUGEPAGE_SIZE (1 << 21)

// TODO : platform defines
// TODO : build configuration defines

// TODO : enforce some checks on definitions

namespace cuw3 {
    using uint = unsigned;

    using uint8 = uint8_t;
    using uint16 = uint16_t;
    using uint32 = uint32_t;
    using uint64 = uint64_t;

    using int8 = int8_t;
    using int16 = int16_t;
    using int32 = int32_t;
    using int64 = int64_t;

    using intptr = intptr_t;
    using uintptr = uintptr_t;

    using ptrdiff = ptrdiff_t;

    using gsize = size_t;
    using usize = std::make_unsigned_t<gsize>;
    using ssize = std::make_signed_t<gsize>;

    template<class T>
    concept SignedInteger = std::signed_integral<T>;
    
    template<class T>
    concept UnsignedInteger = std::unsigned_integral<T>;
    
    template<class T>
    concept Integer = std::integral<T>;

    template<class T>
    concept IntptrLike = std::is_same_v<T, intptr> || std::is_same_v<T, uintptr> || sizeof(T) >= sizeof(uintptr);

    inline constexpr uint64 cacheline = CUW3_CACHELINE_SIZE;
    inline constexpr uint64 control_block_size = CUW3_CONTROL_BLOCK_SIZE;
}