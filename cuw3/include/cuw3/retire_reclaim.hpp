#pragma once

#include <atomic>

#include "ptr.hpp"
#include "assert.hpp"

namespace cuw3 {
    // TODO : check explanations

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
    // yes, after we have reclaimed everything under some retired resource, resource itself may become completely unused => it must be released
    // if root resource (like thread allocator completely exausted) we can free root as there will be no other users of the subresource
    //
    // original owning thread of the root resource (and all of its subresources) is called just owner
    // whatever thread is responsible for reclaiming resources is called reclaiming thread
    //
    // owner in most of the cases serves as reclaiming thread (until it wants to die to dissolve into the void)
    // thus, it raises 'retired' state on the root resource. it snatches retired resources from the root but keeps the retired flag raised.
    // this way no other retiring thread can retire the root (already considered retired) and become reclaiming thread itself.
    // when owning thread desires to die it continuously attempts to release retire status of the root resource reclaiming all resources that it sees
    // ... or it just may want to send it to the graveyard:) from where root can be reclaimed later
    // same strategy applies to any other thread who will successfully become reclaiming after owner died
    // if any thread decides that it has enough of reclaiming and it wants to send this stupid bitch to grave
    // * it must retain retire status
    // * ... move it into the fucking grave already, dumbass
    // * whenever you excavate it from the grave (gain exclusive access) then ... it is no longer in the grave you may not even attempt to put it back
    // * just do not forget to reset grave status and repeat retire flag release porocedure as was mentioned before
    //   * or you will have to put it back into the grave!
    //
    // more concrete matters
    // 1. retired pointer is pointer of the next retired subresource + retired status flag in the alignment
    // it serves as the head of the retired subresources list. this is an atomic field that may be contended for by multiple threads.
    // 2. list pointer of all retired resources of the current level (resource itself can be retired). non-atomic field as access is exclusive (only one thread can reclaim it)
    // so yeah, we need at least two pointers to support retire-reclaime scheme
    // 
    // reclaimng thread snatches the whole list of retired subresources and reclaims all of them.
    // after that it considers it as each resource can be either still in use or become unused.
    // if it becomes unused then it is freed.
    // if it hasn't become unused (there are some subresources in use) reclaimng thread does nothing - all accumulated retired resources will be freed during the next inspection
    //
    // depth of the hierarchy here does not matter
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
    // but we can also store 3 additional status bits within it
    
    using RetireReclaimRawPtr = uint64;
    
    inline constexpr RetireReclaimRawPtr retire_reclaim_flag_bits = 4;
    inline constexpr RetireReclaimRawPtr retire_reclaim_pointer_alignment = 16;

    using RetireReclaimPtr = AlignmentPackedPtr<RetireReclaimRawPtr, retire_reclaim_flag_bits>;

    // NOTE: even though alignment of16 is only required for root as it is the only resource that has all 4 flags
    // but for the sake of simplicity this will be common requirement for all of the resources
    enum class RetireReclaimFlags : RetireReclaimRawPtr {
        // read-write, thread who retires can set this flag, reclaiming thread can reset this flag
        // retiring thread whenever sees that this flag has already been set stops retiring process
        // resource, marked as retired, can be used to signify that this resource will be exclusively operated on
        // and block other retiring threads to become a reclaiming thread
        RetiredFlag = 1,

        // read-only, must be applied to the root only, readonly flag, set once and then never updated again
        RootResourceFlag = 2,

        // read-write, applied to the root resource only, can be reset by the owner only
        // mostly a status flag, exclusive access to the resource can be controlled via unset retired flag
        OwnerAliveFlag = 4,

        // read-write, reclaiming thread postponed resource clean-up and moved root resource to the graveyard
        // mostly a status flag, exclusive access to the resource can be controlled via unset retired flag
        // applied to the root resource only
        GraveyardFlag = 8,
    };

    struct RetireReclaimFlagsHelper {
        RetireReclaimFlagsHelper(RetireReclaimFlags fls) : flags{(RetireReclaimRawPtr)fls} {}
        RetireReclaimFlagsHelper(RetireReclaimRawPtr fls) : flags{fls} {}

        bool root_resource() const { return flags & (RetireReclaimRawPtr)RetireReclaimFlags::RootResourceFlag; }
        bool owner_alive() const { return flags & (RetireReclaimRawPtr)RetireReclaimFlags::OwnerAliveFlag; }
        bool retired() const { return flags & (RetireReclaimRawPtr)RetireReclaimFlags::RetiredFlag; }
        bool graveyard() const { return flags & (RetireReclaimRawPtr)RetireReclaimFlags::GraveyardFlag; }

        RetireReclaimRawPtr flags{};
    };

    struct RetireReclaimPtrView {
        template<class ... Flag>
        static RetireReclaimPtr ptr_with_flags(void* ptr, Flag ... flag) {
            auto flags = (0 | ... | (RetireReclaimRawPtr)flag);
            return RetireReclaimPtr::packed(ptr, flags);
        }
        
        template<class ... Flag>
        static RetireReclaimPtr data_with_flags(RetireReclaimRawPtr data, Flag ... flag) {
            auto flags = (0 | ... | (RetireReclaimRawPtr)flag);
            return RetireReclaimPtr::packed(data, flags);
        }

        static RetireReclaimPtr root_resource() {
            return ptr_with_flags(nullptr, RetireReclaimFlags::RootResourceFlag, RetireReclaimFlags::OwnerAliveFlag);
        }

        // called by reclaiming thread when alive
        // preserves already raised flags
        // retired flag must be set!
        // can be used as for number resource as well
        [[nodiscard]] RetireReclaimPtr reclaim_root() {
            auto resource_ref = std::atomic_ref{*resource};
            auto resource_old = resource_ref.load(std::memory_order_relaxed);
            CUW3_CHECK(RetireReclaimFlagsHelper{resource_old.data()}.root_resource(), "resource must be root");
            CUW3_CHECK(RetireReclaimFlagsHelper{resource_old.data()}.retired(), "retired flag must have been set!");

            // retired is set, nobody can reset it
            // all the other flags are not touched too
            // so instead of cas we can copy flags, set pointer to null and use exchange instead
            auto resource_new = ptr_with_flags(nullptr, resource_old.data());
            return resource_ref.exchange(resource_new, std::memory_order_acq_rel);
        }

        // called by reclaiming thread
        // attempts to reset some flags if retire-reclaim pointer is empty
        // would work for both ptr and number resource representation
        template<class ... Flag>
        [[nodiscard]] bool try_reset_flags(Flag ... flag) {
            auto flags = (0 | ... | (RetireReclaimRawPtr)flag);

            auto resource_ref = std::atomic_ref{*resource};
            auto resource_old = resource_ref.load(std::memory_order_relaxed);
            if (resource_old.ptr()) {
                return false;
            }
            auto resource_new = ptr_with_flags(nullptr, resource_old.data() & ~flags);
            return resource_ref.compare_exchange_strong(resource_old, resource_new, std::memory_order_acq_rel, std::memory_order_relaxed);
        }

        // must be checked on some occasions!
        // called by the thread that wants to retire some resource
        // thread continiously attempts to set new head
        // returns previously observed flags
        // resource_ops must contain only one op: void set_next(void* resource, void* head) {...}
        template<class Backoff, class ResourceOps>
        RetireReclaimFlags retire_ptr(void* retired, Backoff&& backoff, ResourceOps&& resource_ops) {
            CUW3_CHECK(is_aligned(resource, retire_reclaim_pointer_alignment), "resource pointer is not placed ate the properly aligned location");

            auto resource_ref = std::atomic_ref{*resource};
            auto resource_old = resource_ref.load(std::memory_order_relaxed);
            while (true) {
                auto resource_new = ptr_with_flags(resource, resource_old.data(), RetireReclaimFlags::RetiredFlag);

                resource_ops.set_next(retired, resource_old.ptr());
                if (resource_ref.compare_exchange_strong(resource_old, resource_new, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    return (RetireReclaimFlags)resource_old.data();
                }
                backoff();
            }
        }

        // special case when retired resource can be represented as just a number
        template<class Backoff>
        RetireReclaimFlags retire_data(RetireReclaimRawPtr data, Backoff&& backoff) {
            auto resource_ref = std::atomic_ref{*resource};
            auto resource_old = resource_ref.load(std::memory_order_relaxed);
            while (true) {
                auto resource_new = data_with_flags(resource_old.value_shifted() + data, resource_old.data(), RetireReclaimFlags::RetiredFlag);
                if (resource_ref.compare_exchange_strong(resource_old, resource_new, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    return (RetireReclaimFlags)resource_old.data();
                }
                backoff();
            }
        }

        RetireReclaimPtr* resource{};
    };
}