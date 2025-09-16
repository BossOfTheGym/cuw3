#pragma once

#include <atomic>

#include "ptr.hpp"
#include "assert.hpp"

namespace cuw3 {
    // Explanation:
    // we have a resource
    // resource is split down to subresources
    // as long as some subresource is used - resource lives
    //   => all of the resources down (down = going to the root) the hierarchy chain live
    //   as long as some subresource up (up = from the root to the subresource) the hierarchy lives
    //   => it means that all fields are accessible of this 
    // root resource is always alive
    //
    // sometimes we want to retire some subresource so we can either reuse it or retire its parent resource completely in case it becomes completely unused
    // 
    // this is, in fact, what we call to 'retire' here.
    // 'retired' means 'has some subresource to reclaim'. hold for root resource and all intermediate subresources in the hierarchy
    // if subresource is a leave (the highest in the hierarchy, is not further split to ) then if it is 
    //
    // this is used mainly by threads that do not own the allocated memory so they have to postpone its deallocation
    // non-owning thread retires some subresource and owning thread reclaims it later
    // that is why it is called retire-reclaim scheme
    //
    // only one thread must retire some subresource at once
    // only one thread must reclaim some subresources at once
    // retiring thread does not know if the parent resource becomes unused so it never checks this fact - it just retires parent resource as well
    // yes, thread that retires some subresource must retire whole down-the-root-going chain of dependent resources
    // no, if some resource is retired it does not mean that this reource is completely unused: only that there is some subresource up the hierarchy
    //   that must be reclaimed.
    // yes, after we have reclaimed everything under some retired resource, resource itself may become completely unused => it must be released (if it is not root resource)
    //
    //
    // more concrete matters
    // 1. retired pointer is pointer of the next retired subresource + retired status flag in the alignment
    // it serves as the head of the retired subresources list. this is an atomic field that is may be contended for by multiple threads.
    // 2. list pointer of all retired resources of the current level (resource itself can be retired). non-atomic field as access is exclusive (only one thread can reclaim it)
    // so yeah, we need at least two field to support retire-reclaime scheme
    // 
    // reclaimng thread snatches the whole list of retired subresources and reclaims all of them.
    // after that it considers it as each resource can be either still in use or become unused.
    // if it becomes unused then it is freed.
    // if it hasn't become unused (there are some subresources in use) reclaimng thread does nothing - all accumulated retired resources will be freed during the next inspection
    //
    // depth of the hierrachy here does not matter
    // this is a lock-free algorithm

    // TODO : move it somewhere
    // implementation forethought
    //
    // handle list pointer (32 bits)
    // thread pointer, allocation ds type
    // mt retiration pointer + retired status flag
    // maybe another list pointer to postpone reclamation if we need to reclaim way too much (it may delay current operation too much)
    //
    // let's sum up:
    // 1. thread pointer + allocator type | handle next + none -> offset ptr (universal handle header) TODO : impose alignment constraint on thread allcoator
    // 2. retire-reclaim pointer -> offset ptr
    // 3. list pointer to store all of the retired handles
    // 4. maybe list pointer that stores some postponed reclaimed resources
    // 
    // 1 is never changed after handle is acquired. so we can store it in the first cacheline
    // 2 and 3 are modified only in retire-reclaim scenario by some other non-owning thread at first => can be placed in the second cacheline
    // 4 may go with 2 and 3 as well
    //
    // we need only one bit to store retired flag, default alignment is always greater than 2 => we can store this in any pointer
    // also we don't have to store pointer in the pointer field: it can be whatever data we need (amount of retired resource)
    // also! we can use retired status to 'lock' some resource to transfer its ownership
    
    using RetireReclaimRawPtr = uint64;
    
    inline constexpr RetireReclaimRawPtr retire_reclaim_flag_bits = 1;

    using RetireReclaimPtr = AlignmentPackedPtr<RetireReclaimRawPtr, retire_reclaim_flag_bits>;

    enum class RetireReclaimRetireResult {
        AlreadyRetired,
        JustRetired
    };

    struct RetireReclaimPtrView {
        static RetireReclaimPtr empty_head() {
            return RetireReclaimPtr::packed(nullptr, 0);
        }

        // called by reclaiming thread (exclusively)
        // completely snatches the whole list of the retired resources
        RetireReclaimPtr reclaim() {
            auto ptr_ref = std::atomic_ref{*ptr};
            return ptr_ref.exchange(empty_head(), std::memory_order_acq_rel);
        }

        // called by the thread that wants to retire some resource
        // thread continiously attempts to set new head
        // if it succeeds then status of retirement is returned:
        // * if we must continue to retire
        // * or we should stop retiring as there is another thread already doing this
        template<class Backoff, class UpdateNext>
        RetireReclaimRetireResult retire(void* resource, Backoff&& backoff, UpdateNext&& update_next) {
            auto ptr_ref = std::atomic_ref{*ptr};
            auto ptr_old = ptr_ref.load(std::memory_order_relaxed);
            auto ptr_new = RetireReclaimPtr::packed(resource, 1);
            while (true) {
                update_next(ptr_old.ptr());   
                if (ptr_ref.compare_exchange_strong(ptr_old, ptr_new, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    break;
                }
                backoff();
            }
            return ptr_old.data() ? RetireReclaimRetireResult::AlreadyRetired : RetireReclaimRetireResult::JustRetired;
        }

        // called by reclaiming thread (exclusively)
        // attempt to mark some resource as retired, usually if we do want to alter its state
        // all the other threads attempting to retire their subresources will not be able to proceed past this resource
        // => we are able to modify its state
        RetireReclaimRetireResult try_lock() {
            auto ptr_ref = std::atomic_ref{*ptr};
            auto ptr_old = ptr_ref.load(std::memory_order_relaxed);
            if (ptr_old.data()) {
                CUW3_ASSERT(ptr_old.ptr(), "possible non-exclusive access: retired flag raised but pointer is null.");
                return RetireReclaimRetireResult::AlreadyRetired;
            }
            auto ptr_new = RetireReclaimPtr::packed(ptr_old.ptr(), 1);
            if (ptr_ref.compare_exchange_strong(ptr_old, ptr_new, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                return RetireReclaimRetireResult::JustRetired;
            }
            return RetireReclaimRetireResult::AlreadyRetired;
        }

        RetireReclaimPtr* ptr{};
    };
}