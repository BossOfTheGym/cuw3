#pragma once

#include "conf.hpp"
#include "list.hpp"
#include "utils.hpp"
#include "atomic.hpp"
#include "backoff.hpp"


namespace cuw3 {
    // grave consists of slots and common retire list
    // first, you attempt to fill the slots, after that (if you failed) you push it to the common list
    
    using ThreadGraveyardBackoff = SimpleBackoff;

    struct alignas(conf_cacheline) ThreadGraveEntry {
        uint64 lock{}; // atomic
        void* data{}; // atomic
    };

    struct ThreadGraveEntryView {
        // acquired grave must not be empty
        // locks the entry on success
        [[nodiscard]] void* try_acquire() {
            auto lock_ref = std::atomic_ref{grave_entry_ptr->lock};
            auto data_ref = std::atomic_ref{grave_entry_ptr->data};

            if (!data_ref.load(std::memory_order_relaxed)) {
                return nullptr;
            }
            if (lock_ref.load(std::memory_order_relaxed) == 1) {
                return nullptr;
            }
            if (lock_ref.exchange(1, std::memory_order_acquire) == 1) {
                return nullptr;
            }
            void* acquired = data_ref.load(std::memory_order_relaxed);
            if (!acquired) {
                lock_ref.store(0, std::memory_order_release);
            }
            return acquired;
        }

        // make an attempt to put thread into the grave
        // lock is always released in the end
        [[nodiscard]] bool try_put(void* thread) {
            auto lock_ref = std::atomic_ref{grave_entry_ptr->lock};
            auto data_ref = std::atomic_ref{grave_entry_ptr->data};

            if (data_ref.load(std::memory_order_relaxed)) {
                return false;
            }
            if (lock_ref.load(std::memory_order_relaxed) == 1) {
                return false;
            }
            if (lock_ref.exchange(1, std::memory_order_acquire) == 1) {
                return false;
            }
            void* prev = data_ref.load(std::memory_order_relaxed);
            if (prev == nullptr) {
                data_ref.store(thread, std::memory_order_relaxed);
            }
            lock_ref.store(0, std::memory_order_release);
            return prev == nullptr;
        }

        // release lock if we previously acquired grave
        void release() {
            auto lock_ref = std::atomic_ref{grave_entry_ptr->lock};
            CUW3_CHECK(lock_ref.load(std::memory_order_relaxed) == 1, "lock was not acquired during emptying process");

            lock_ref.store(0, std::memory_order_release);
        }

        // empty grave slot after we previously acquired it and release the lock
        void empty_grave() {
            auto lock_ref = std::atomic_ref{grave_entry_ptr->lock};
            auto data_ref = std::atomic_ref{grave_entry_ptr->data};
            CUW3_CHECK(lock_ref.load(std::memory_order_relaxed) == 1, "lock was not acquired during emptying process");
            CUW3_CHECK(data_ref.load(std::memory_order_relaxed) != nullptr, "data must not have been nullptr");

            data_ref.store(nullptr, std::memory_order_relaxed);
            lock_ref.store(0, std::memory_order_release);
        }

        bool is_empty() {
            auto data_ref = std::atomic_ref{grave_entry_ptr->data};
            return !data_ref.load(std::memory_order_relaxed);
        }

        ThreadGraveEntry* grave_entry_ptr{};
    };

    enum class ThreadGraveDataStatus : uint32 {
        Valid,
        Failed,
        Null,
    };

    struct ThreadGraveData {
        explicit operator bool() const {
            return data;
        }

        void* data{};
        uint32 grave{};
    };

    struct ThreadGraveAcquireParams {
        uint rounds = 1; // never infinite
        uint start = 0;
        uint step = 1; // never even
    };

    struct ThreadGraveDeadQueueTraits {
        using LinkType = void*;

        static constexpr LinkType null_link = nullptr;
    };

    using ThreadGraveDeadQueue = AtomicPushSnatchList<ThreadGraveDeadQueueTraits>;
    using ThreadGraveyardBackoff = SimpleBackoff;

    struct alignas(conf_cacheline) DefaultThreadGraveyardEntry {
        DefaultThreadGraveyardEntry* next{};
        DefaultThreadGraveyardEntry* skip{};
    };
    
    // default list ops for graveyards
    // manipulates DefaultThreadGraveyardEntry entities
    struct DefaultThreadGraveyardOps {
        void set_next(void* node, void* next) {
            ((DefaultThreadGraveyardEntry*)node)->next = (DefaultThreadGraveyardEntry*)next;
        }

        void* get_next(void* node) {
            return ((DefaultThreadGraveyardEntry*)node)->next;
        }

        void reset_next(void* node) {
            ((DefaultThreadGraveyardEntry*)node)->next = nullptr;
        }

        void set_skip(void* node, void* skip) {
            ((DefaultThreadGraveyardEntry*)node)->skip = (DefaultThreadGraveyardEntry*)skip;
        }

        void* get_skip(void* node) {
            return ((DefaultThreadGraveyardEntry*)node)->skip;
        }

        void reset_skip(void* node) {
            ((DefaultThreadGraveyardEntry*)node)->skip = (DefaultThreadGraveyardEntry*)node;
        }
    };

    // will use simplified grave inspection algorithm
    struct ThreadGraveyard {
        [[nodiscard]] static ThreadGraveyard* create(Memory memory, uint num_grave_entries) {
            CUW3_CHECK_RETURN_VAL(memory.fits<ThreadGraveyard>(), nullptr, "invalid memory");
            CUW3_CHECK_RETURN_VAL(is_pow2(num_grave_entries), nullptr, "number of graves must be pow of 2");
            CUW3_CHECK_RETURN_VAL(num_grave_entries <= conf_graveyard_slot_count, nullptr, "too big amount of graves");

            auto* graveyard = new (memory.get()) ThreadGraveyard{};
            graveyard->num_grave_entries = num_grave_entries;
            return graveyard;
        }


        [[nodiscard]] ThreadGraveData acquire_(ThreadGraveAcquireParams params) {
            ThreadGraveyardBackoff backoff{};
            for (uint round = 0; round < params.rounds; round++) {
                for (
                    uint slot = params.start & (num_grave_entries - 1), i = 0;
                    i < num_grave_entries;
                    slot = (slot + params.step) & (num_grave_entries - 1), i++
                ) {
                    auto grave_entry_view = ThreadGraveEntryView{&grave_entries[slot]};
                    if (auto acquired = grave_entry_view.try_acquire()) {
                        return ThreadGraveData{acquired, slot};
                    }
                }
                backoff();
            }
            return {};
        }

        void release_(ThreadGraveData data) {
            CUW3_CHECK(data.grave != num_grave_entries, "invalid grave");

            auto grave_entry_view = ThreadGraveEntryView{&grave_entries[data.grave]};
            grave_entry_view.release();
        }

        void empty_grave_(ThreadGraveData data) {
            CUW3_CHECK(data.grave != num_grave_entries, "invalid grave");

            auto grave_entry_view = ThreadGraveEntryView{&grave_entries[data.grave]};
            grave_entry_view.empty_grave();
        }

        template<class NodeOps>
        [[nodiscard]] void* distribute_(NodeOps&& node_ops, void* head, uint rounds) {
            void* curr = head;
            for (uint round = 0; curr && round < rounds; round++) {
                uint distributed = 0;
                for (uint i = 0; curr && i < num_grave_entries; i++) {
                    void* next = node_ops.get_next(curr);
                    auto grave_entry_view = ThreadGraveEntryView{&grave_entries[i]};
                    if (grave_entry_view.try_put(curr)) {
                        curr = next;
                        distributed++;
                    }
                }
                if (distributed == 0) {
                    break;
                }
            }
            return curr;
        }

        template<class NodeOps>
        [[nodiscard]] ThreadGraveData acquire_distribute_(NodeOps&& node_ops) {
            ThreadGraveDeadQueue grave_list{&dead_queue};
            
            void* snatched = grave_list.snatch();
            if (!snatched) {
                return {};
            }
            
            void* distributed = node_ops.get_next(snatched);
            void* remaining = distribute_(node_ops, distributed, 2);
            grave_list.push(remaining, ThreadGraveyardBackoff{}, node_ops);

            return {snatched, num_grave_entries};
        }

        bool put_thread_(void* thread, ThreadGraveAcquireParams params) {
            for (uint round = 0; round < params.rounds; round++) {
                for (
                    uint slot = (params.start + params.step) & (num_grave_entries - 1), i = 0;
                    i < num_grave_entries;
                    slot = (slot + params.step) & (num_grave_entries - 1), i++
                ) {
                    auto grave_entry_view = ThreadGraveEntryView{&grave_entries[slot]};
                    if (grave_entry_view.try_put(thread)) {
                        return true;
                    }
                }
            }
            return false;
        }

        template<class NodeOps>
        void enqueue_thread_(void* thread, NodeOps&& node_ops) {
            auto dead_queue_view = ThreadGraveDeadQueue{&dead_queue};
            node_ops.reset_next(thread);
            node_ops.reset_skip(thread);
            dead_queue_view.push(thread, ThreadGraveyardBackoff{}, node_ops);
        }


        // API
        // acquire retired thread
        template<class NodeOps>
        [[nodiscard]] ThreadGraveData acquire(NodeOps&& node_ops, ThreadGraveAcquireParams params) {
            if (auto acquired = acquire_(params)) {
                return acquired;
            }
            return acquire_distribute_(node_ops);
        }

        // thread is no longer needed here
        void empty_grave(ThreadGraveData grave_data) {
            CUW3_CHECK(grave_data.grave <= num_grave_entries, "invalid grave num");

            if (grave_data.grave < num_grave_entries) {
                empty_grave_(grave_data);
            }
        }

        // thread was in grave and we want to put it back
        template<class NodeOps>
        void release_thread(ThreadGraveData grave_data, NodeOps&& node_ops) {
            CUW3_CHECK(grave_data.grave <= num_grave_entries, "invalid grave num provided");

            if (grave_data.grave < num_grave_entries) {
                release_(grave_data);
            }
            if (grave_data.grave == num_grave_entries) {
                put_thread(grave_data.data, node_ops);
            }
        }

        // thread is dead, was never dead before, so we want to put it into the grave
        template<class NodeOps>
        void put_thread(void* thread, NodeOps&& node_ops) {
            if (put_thread_(thread, {})) {
                return;
            }
            enqueue_thread_(thread, node_ops);
        }

            // for testing purposes only
        bool is_empty() {
            for (uint i = 0; i < num_grave_entries; i++) {
                if (!ThreadGraveEntryView{&grave_entries[i]}.is_empty()) {
                    return false;
                }
            }
            return !std::atomic_ref{dead_queue}.load(std::memory_order_relaxed);
        }


        ThreadGraveEntry grave_entries[conf_graveyard_slot_count] = {}; // atomic

        struct alignas(conf_cacheline) {
            void* dead_queue{}; // atomic
        };
        
        uint num_grave_entries{}; // readonly
    };
}