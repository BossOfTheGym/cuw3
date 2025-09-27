#include <cstdint>
#include <cstddef>
#include <concepts>
#include <type_traits>

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

    template<class T>
    concept VoidLike = std::is_void_v<T>;

    template<class T>
    concept NonReferenceType = std::is_same_v<T, std::remove_reference_t<T>>;

    template<class T, NonReferenceType U>
    using SameConstAs = std::conditional_t<std::is_const_v<T>, const U, std::remove_cv_t<U>>;
}