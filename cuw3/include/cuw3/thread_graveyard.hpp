#pragma once

#include "conf.hpp"
#include "cuw3/atomic.hpp"
#include "list.hpp"
#include "backoff.hpp"
#include "retire_reclaim.hpp"

#define CUW3_GRAVEYARD_SLOT_COUNT 16

namespace cuw3 {
    inline constexpr usize conf_graveyard_slot_count = CUW3_GRAVEYARD_SLOT_COUNT;

    // grave consists of slots and common retire list
    // first, you attempt to fill the slots, after that (if you failed) you push it to the common list
    
    using ThreadGraveyardBackoff = SimpleBackoff;

    using ThreadGraveRawPtr = uint64;

    inline constexpr ThreadGraveRawPtr thread_grave_status_bits = 1;

    enum ThreadGravePtrFlags : ThreadGraveRawPtr {
        // when set, means that somebody acquired this slot
        // distributing thread must not put any threads here as it may be occupied back
        // if slot is not occupied and empty => it is just empty
        // if thread has already been acquired then ptr must be null and acquire flag must be raised
        // valid grave states:
        //   ptr == null && state == acquired (grave is not empty but acquired)
        //   ptr == null && state != acquired (grave is empty: no thread and state is not acquired)
        //   ptr != null && state != acquired (grave is not empty and not acquired)
        // invalid state:
        //   ptr != null && state == acquired (grave is not empty but seems like somebody acquired it)
        Acquired = 1,
    };

    using ThreadGravePtr = AlignmentPackedPtr<ThreadGraveRawPtr, 1>;
    
    struct ThreadGravePtrHelper {
        static ThreadGravePtr acquired_state() { return ThreadGravePtr::packed(nullptr, (ThreadGraveRawPtr)ThreadGravePtrFlags::Acquired); }
        static ThreadGravePtr empty_state() { return {}; }

        ThreadGravePtrHelper() = default;

        ThreadGravePtrHelper(ThreadGravePtr ptr) : _ptr{ptr} {}

        bool acquired() const { return flags() & (ThreadGraveRawPtr)ThreadGravePtrFlags::Acquired; }
        bool valid() const { return !(_ptr.ptr() && acquired()); }
        bool empty() const { return !_ptr.ptr(); }

        ThreadGraveRawPtr flags() const { return _ptr.alignment(); }
        void* thread() const { return _ptr.ptr(); }

        ThreadGravePtr _ptr{};
    };

    struct ThreadGravePtrView {
        // acquired grave must not be empty
        [[nodiscard]] ThreadGravePtr try_acquire() {
            auto grave_ptr_ref = std::atomic_ref{*grave_ptr};
            auto grave_ptr_old = grave_ptr_ref.load(std::memory_order_relaxed);
            CUW3_CHECK(ThreadGravePtrHelper{grave_ptr_old}.valid(), "invalid grave state detected");

            if (ThreadGravePtrHelper{grave_ptr_old}.empty()) {
                return grave_ptr_old;
            }

            auto grave_ptr_new = ThreadGravePtrHelper::acquired_state();
            grave_ptr_old = grave_ptr_ref.exchange(grave_ptr_new, std::memory_order_acq_rel);
            CUW3_CHECK(ThreadGravePtrHelper{grave_ptr_old}.valid(), "invalid grave state detected");

            return grave_ptr_old;
        }

        // make an attempt to put thread into the grave
        bool try_put_thread(void* thread) {
            auto grave_ptr_ref = std::atomic_ref{*grave_ptr};
            auto grave_ptr_old = grave_ptr_ref.load(std::memory_order_relaxed);
            auto grave_ptr_new = ThreadGravePtr::packed(thread, 0);
            if (!ThreadGravePtrHelper{grave_ptr_old}.empty()) {
                return false;
            }
            return grave_ptr_ref.compare_exchange_strong(grave_ptr_old, grave_ptr_new, std::memory_order_acq_rel, std::memory_order_relaxed);
        }

        // ptr can be whatever you like (null would simply mean that you want to free the grave)
        // acquired flag must not be set
        ThreadGravePtr release(ThreadGravePtr grave_ptr_new) {
            CUW3_CHECK(!ThreadGravePtrHelper{grave_ptr_new}.acquired(), "new state must not have acquired flag raised");

            auto grave_ptr_ref = std::atomic_ref{*grave_ptr};
            auto grave_ptr_old = grave_ptr_ref.exchange(grave_ptr_new, std::memory_order_acq_rel);
            CUW3_CHECK(ThreadGravePtrHelper{grave_ptr_old}.valid(), "invalid grave state detected");
            CUW3_CHECK(ThreadGravePtrHelper{grave_ptr_old}.acquired(), "grave must have been in acquired state");

            return grave_ptr_old;
        }

        // make grave empty
        void release_grave() {
            release(ThreadGravePtrHelper::empty_state());
        }

        // return thread back to grave (it is not yet fully free)
        // ptr != null
        void put_thread_back(void* thread) {
            CUW3_CHECK(thread, "grave must not be empty");
            release(ThreadGravePtr::packed(thread, 0));
        }

        ThreadGravePtr* grave_ptr{};
    };

    struct alignas(conf_cacheline) ThreadGraveEntry {
        ThreadGravePtr grave{}; // atomic
    };

    enum class ThreadGraveDataStatus : uint32 {
        Valid,
        Failed,
        Null,
    };

    struct ThreadGraveData {
        bool valid() const { return status == ThreadGraveDataStatus::Valid; }
        bool failed() const { return status == ThreadGraveDataStatus::Failed; }
        bool null() const { return status == ThreadGraveDataStatus::Null; }

        ThreadGraveDataStatus status{};
        uint32 grave_num{};
        void* thread{};
    };

    struct ThreadGraveDataHelper {
        static ThreadGraveData valid(uint32 grave_num, void* thread) { return {ThreadGraveDataStatus::Valid, grave_num, thread}; }
        static ThreadGraveData failed() { return {ThreadGraveDataStatus::Failed}; }
        static ThreadGraveData null() { return {ThreadGraveDataStatus::Null}; }
    };

    struct ThreadGraveAcquireParams {
        uint rounds = 1;
        uint start = 0;
        uint step = 1;
    };

    struct ThreadAuxGraveListTraits {
        using LinkType = void*;

        static constexpr LinkType null_link = nullptr;
    };

    using ThreadAuxGraveList = AtomicPushSnatchList<ThreadAuxGraveListTraits>;
    using ThreadGraveyardBackoff = SimpleBackoff;

    // will use simplified grave inspection algorithm
    struct ThreadGraveyard {
        static bool init(ThreadGraveyard* graveyard, uint num_grave_entries) {
            graveyard->num_grave_entries = num_grave_entries;
            return true;
        }

        static bool initz(ThreadGraveyard* graveyard, uint num_grave_entries) {
            *graveyard = {};
            return init(graveyard, num_grave_entries);
        }

        [[nodiscard]] ThreadGraveData _acquire(ThreadGraveAcquireParams acquire_params) {
            for (
                uint curr = acquire_params.start & (num_grave_entries - 1), k = 0;
                k < conf_graveyard_slot_count;
                k++, curr = (curr + acquire_params.step) & (num_grave_entries - 1)
            ) {
                auto grave_view = ThreadGravePtrView{&grave_entries[curr].grave};
                auto grave_ptr = ThreadGravePtrHelper{grave_view.try_acquire()};
                if (grave_ptr.acquired()) {
                    return ThreadGraveDataHelper::failed();
                }
                return ThreadGraveDataHelper::valid(curr, grave_ptr.thread());
            }
            return ThreadGraveDataHelper::null();
        }

        template<class NodeOps>
        [[nodiscard]] void* _distribute(void* thread_list, NodeOps&& node_ops) {
            void* curr_thread = thread_list;
            for (uint curr = 0; curr_thread && curr < num_grave_entries; curr++) {
                void* next_thread = node_ops.get_next(curr_thread);
                auto grave_ptr_view = ThreadGravePtrView{&grave_entries[curr].grave};
                if (grave_ptr_view.try_put_thread(curr_thread)) {
                    curr_thread = next_thread;
                }
            }
            return curr_thread;
        }

        template<class NodeOps>
        [[nodiscard]] ThreadGraveData _acquire_distribute(NodeOps&& node_ops) {
            auto aux_graves_view = ThreadAuxGraveList{&aux_graves};

            void* snatched = aux_graves_view.snatch();
            if (!snatched) {
                return ThreadGraveDataHelper::null();
            }

            void* rest = _distribute(node_ops.get_next(snatched), node_ops);
            if (rest) {
                aux_graves_view.push(rest, ThreadGraveyardBackoff{}, node_ops);
            }
            return ThreadGraveDataHelper::valid(num_grave_entries, snatched);
        }

        // API
        template<class NodeOps>
        [[nodiscard]] ThreadGraveData acquire(NodeOps&& node_ops, ThreadGraveAcquireParams acquire_params) {
            ThreadGraveyardBackoff backoff{};
            for (uint round = acquire_params.rounds; round != 0; ) {
                auto grave_data = _acquire(acquire_params);
                if (grave_data.valid()) {
                    return grave_data;
                }
                if (grave_data.null()) {
                    round -= round > 0;
                }
                backoff();
            }
            return _acquire_distribute(node_ops);
        }

        void release_thread(ThreadGraveData grave_data) {
            CUW3_CHECK(grave_data.valid() && grave_data.thread && grave_data.grave_num <= num_grave_entries, "invalid entry provided");

            if (grave_data.grave_num < num_grave_entries) {
                auto grave_ptr_view = ThreadGravePtrView{&grave_entries[grave_data.grave_num].grave};
                grave_ptr_view.release_grave();
            }
        }

        template<class NodeOps>
        void put_thread_back(ThreadGraveData grave_data, NodeOps&& node_ops) {
            CUW3_CHECK(grave_data.valid() && grave_data.thread && grave_data.grave_num <= num_grave_entries, "invalid entry provided");

            if (grave_data.grave_num < num_grave_entries) {
                auto grave_ptr_view = ThreadGravePtrView{&grave_entries[grave_data.grave_num].grave};
                grave_ptr_view.put_thread_back(grave_data.thread);
            } else {
                auto aux_graves_list = ThreadAuxGraveList{&aux_graves};
                aux_graves_list.push(grave_data.thread, ThreadGraveyardBackoff{}, node_ops);
            }
        }

        template<class NodeOps>
        void put_thread_to_rest(void* thread, NodeOps&& node_ops) {
            CUW3_CHECK(thread, "attempt to put nullptr thread into the grave");

            node_ops.reset_tail(thread);
            node_ops.reset_next(thread);
            if (_distribute(thread, node_ops)) {
                auto aux_grave_list = ThreadAuxGraveList{&aux_graves};
                aux_grave_list.push(thread, ThreadGraveyardBackoff{}, node_ops);
            }
        }


        ThreadGraveEntry grave_entries[conf_graveyard_slot_count] = {}; // atomic

        struct alignas(conf_cacheline) {
            void* aux_graves{}; // atomic
        };
        
        uint num_grave_entries{}; // readonly
    };
}