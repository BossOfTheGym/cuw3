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
            return !empty() && size_ >= sizeof(T) && is_aligned(ptr_, alignof(T));
        }

        template<class T>
        bool fits(uint64 alignment) const {
            return !empty() && size_ >= sizeof(T) && is_aligned(ptr_, std::max(alignof(T), alignment));
        }

        template<class T>
        bool fits(uint64 size, uint64 alignment) const {
            return !empty() && size_ >= std::max(sizeof(T), size) && is_aligned(ptr_, std::max(alignof(T), alignment));
        }

        bool fits(uint64 size, uint64 alignment) const {
            return !empty() && size_ >= size && is_aligned(ptr_, alignment);
        }

        bool empty() const {
            return !ptr_;
        }


        void* get() const {
            return ptr_;
        }

        uintptr size() const {
            return size_;
        }

        explicit operator bool() const {
            return !empty();
        }


        void* ptr_{};
        uintptr size_{};
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