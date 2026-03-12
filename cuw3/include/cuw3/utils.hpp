#pragma once

#include "ptr.hpp"
#include "defs.hpp"
#include "funcs.hpp"

namespace cuw3 {
    struct Memory {
        template<class T>
        static Memory from(T* memory) {
            return {memory, (uintptr)sizeof(T)};
        }


        template<class T>
        bool fits() const {
            return !empty() && _size >= sizeof(T) && is_aligned(_ptr, alignof(T));
        }

        template<class T>
        bool fits(uint64 alignment) const {
            return !empty() && _size >= sizeof(T) && is_aligned(_ptr, std::max(alignof(T), alignment));
        }

        template<class T>
        bool fits(uint64 size, uint64 alignment) const {
            return !empty() && _size >= std::max(sizeof(T), size) && is_aligned(_ptr, std::max(alignof(T), alignment));
        }

        bool fits(uint64 size, uint64 alignment) const {
            return !empty() && _size >= size && is_aligned(_ptr, alignment);
        }

        bool empty() const {
            return !_ptr;
        }


        void* get() const {
            return _ptr;
        }

        uintptr size() const {
            return _size;
        }

        explicit operator bool() const {
            return !empty();
        }


        void* _ptr{};
        uintptr _size{};
    };
}