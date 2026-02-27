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
            return !empty() && sizeof(T) >= size && is_aligned(ptr, alignof(T));
        }

        void* get() const {
            return ptr;
        }

        bool empty() const {
            return ptr;
        }

        explicit operator bool() const {
            return empty();
        }

        void* ptr{};
        uintptr size{};
    };
}