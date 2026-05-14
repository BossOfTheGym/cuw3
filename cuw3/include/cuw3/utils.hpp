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

        static Memory from(void* memory, uintptr size) {
            return {memory, size};
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

    struct AcquiredResource {
        enum class Status {
            Failed,
            Acquired,
            NoResource,
        };

        static AcquiredResource acquired(void* resource) {
            return {Status::Acquired, resource};
        }

        static AcquiredResource no_resource() {
            return {Status::NoResource};
        }

        static AcquiredResource failed() {
            return {Status::Failed};
        }

        void* get() const {
            return resource;
        }

        bool status_failed() const {
            return status == Status::Failed;
        }

        bool status_acquired() const {
            return status == Status::Acquired;
        }

        bool status_no_resource() const {
            return status == Status::NoResource;
        }

        Status status{};
        void* resource{};
    };

    template<class T>
    struct AcquiredTypedResource : AcquiredResource {
        static AcquiredTypedResource acquired(void* resource) {
            return {AcquiredResource::acquired(resource)};
        }

        static AcquiredTypedResource no_resource() {
            return {AcquiredResource::no_resource()};
        }

        static AcquiredTypedResource failed() {
            return {AcquiredResource::failed()};
        }

        T* get() const {
            return (T*)AcquiredResource::get();
        }
    };
}