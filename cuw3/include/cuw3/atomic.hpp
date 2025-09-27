#pragma once

#include <atomic>

#include "defs.hpp"

namespace cuw3 {
    // This right here is ... somewhat viable implementation that will probably be used.
    // Somewhat because in theory version counter may overflow to the value some other modifying thread has previously read so program execution becomes undefined.
    // This should never happen in practice... Should not, at least.
    // Version hack allows us cheaply implement 32-bit software transactional memory. But still, no 100% ork guarantee.
    // The more bits we use for the version, the less is the probability of smething bad happening.
    // cmpxchg16 would make things even more robust as we would be able to use plain pointer (64 bits) + 64 bits for the version
    //
    // This is push/pop atomic list. In it's simplest form. No atomic_shared_ptr required. Yes, there is no 100% work guarantee due to version.
    // To make push/pop list implementable we will use one simple fact: all list entries always exist in the valid memory location.
    // This way, we can always read some garbage value from the location if it was modified. We will not be able to complete list modification anyway.
    // To track list modification (well, to know that list was modified) we will use versioned head.
    // This will help to mitigate ABA problem at some extent. To make it even more robust you can increase number of version bits. But still, no 100% guarantee. 
    // 
    // We must remember that some entry that we exclusively popped from th elist must be exclusively pushed into it.
    // This is pretty obvious but must be stated explicitely: only one thread thread can push an entry back to the list.
    //
    // We can also implement version with count hint at a cost of smaller bit-width of the version field.

    // Example:
    // 
    // using ExampleLinkType = uint64;
    // 
    // struct ExampleExternalDataNode {
    //     ExampleLinkType next{};
    //     void* very_important_node_specific_data{};
    // };
    // 
    // struct ExampleAtomicListTraits {
    //     using LinkType = ExampleLinkType;
    // 
    //     struct ListHead {
    //         LinkType version:32, next:32;
    //     };
    //     
    //     static constexpr LinkType null_link = 0xFFFFFFFF;
    //     static constexpr LinkType op_failed = 0xFFFFFFFE;
    // };
    // 
    // struct ExampleExternalDataOps {
    //     void set_next(ExampleLinkType node, ExampleLinkType next) {
    //          std::atomic_ref{nodes[node].next}.store(next, std::memory_order_relaxed);
    //     }
    // 
    //     uint64 get_next(ExampleLinkType node) {
    //         return std::atomic_ref{nodes[node].next}.load(std::memory_order_relaxed);
    //     }
    // 
    //     ExampleExternalDataNode* nodes{};
    // };
    // 
    // - ListHead must have version and head fields
    // - ListHead must be constructible from two consecutive numbers: version & next
    // - ExternalDataOps must provide two functions: set_next and get_next to manipulate links of external data
    template<class AtomicListTraits>
    struct AtomicListView {
        using Traits = AtomicListTraits;
        using LinkType = typename Traits::LinkType;
        using ListHead = typename Traits::ListHead;

        static constexpr LinkType null_link = Traits::null_link;
        static constexpr LinkType op_failed = Traits::op_failed;

        template<class Backoff, class ExternalDataOps>
        bool push(int attempts, LinkType node, Backoff&& backoff, ExternalDataOps&& external_data_ops) {
            auto head_ref = std::atomic_ref{*head};
            auto head_old = head_ref.load(std::memory_order_relaxed);
            for (int curr = attempts; curr != 0; curr -= curr > 0) {
                external_data_ops.set_next(node, head_old.next);

                auto head_new = ListHead{head_old.version + 1, node};
                if (head_ref.compare_exchange_strong(head_old, head_new, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    return true;
                }
                backoff();
            }
            return false;
        }
        
        template<class Backoff, class ExternalDataOps>
        void push(LinkType node, Backoff&& backoff, ExternalDataOps&& external_data_ops) {
            push(-1, node, backoff, external_data_ops);
        }

        template<class Backoff, class ExternalDataOps>
        LinkType pop(int attempts, Backoff&& backoff, ExternalDataOps&& external_data_ops) {
            auto head_ref = std::atomic_ref{*head};
            auto head_old = head_ref.load(std::memory_order_relaxed);
            for (int curr = attempts; curr != 0; curr -= curr > 0) {
                if (head_old.next == null_link) {
                    return null_link;
                }
                auto head_new = ListHead{head_old.version + 1, external_data_ops.get_next(head_old.next)};
                if (head_ref.compare_exchange_strong(head_old, head_new, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    return head_old.next;
                }
                backoff();
            }
            return op_failed;
        }

        template<class Backoff, class ExternalDataOps>
        LinkType pop(Backoff&& backoff, ExternalDataOps&& external_data_ops) {
            return pop(-1, backoff, external_data_ops);
        }

        ListHead* head{};
    };

    // This is grow-only stack. Thus, it has only one method bump().
    // Usually it is part of pool implementation.
    // Used when list is exausted so we need to grab some fresh entry.
    //
    // Traits can look like AtomicListTraits
    //
    // For now, bump() always succeeds
    template<class AtomicBumpStackTraits>
    struct AtomicBumpStackView {
        using Traits = AtomicBumpStackTraits;
        using LinkType = typename Traits::LinkType;

        static constexpr LinkType null_link = Traits::null_link;
        static constexpr LinkType op_failed = Traits::op_failed;

        LinkType bump() {
            auto top_ref = std::atomic_ref{*top};
            auto top_old = top_ref.load(std::memory_order_relaxed);
            if (top_old >= limit) {
                return null_link;
            }
            top_old = top_ref.fetch_add(1, std::memory_order_acq_rel);
            if (top_old < limit) {
                return top_old;
            }
            // we don't really care about overshooting, we can omit this completely
            // but if we really did then we would have used cas loop in the code above
            // NOTE : this can be made platform specific as fetch_sub can be implemented via cas-loop itself
            top_ref.fetch_sub(1, std::memory_order_acq_rel);
            return null_link;
        }

        LinkType* top{};
        LinkType limit{};
    };
}