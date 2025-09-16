#pragma once

#include <atomic>

#include "defs.hpp"

namespace cuw3 {
    // Explanation:
    // This right here is ... somewhat viable implementation that will probably be used
    // This is push/pop atomic list. In it's simplest form. No atomic_shared_ptr required.
    // To make push/pop list implementable we will use one simple fact: all list entries always exist in the valid memory location.
    // This way, we can always read some garbage value if it ever was modified.
    // To track list modification (well, to know that list was modified) we will use versioned head.
    // This will help to mitigate ABA problem at some extent. To make it even more robust you can even 
    // 
    // We can also implement version with count hint at a cost of smaller bit-width of the version field

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
    // };
    // 
    // struct ExampleExternalDataOps {
    //     void set_next(ExampleLinkType node, ExampleLinkType next) {
    //         nodes[node].next = next;
    //     }
    // 
    //     uint64 get_next(ExampleLinkType node) {
    //         return nodes[node].next;
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
        using LinkType = typename AtomicListTraits::LinkType;
        using ListHead = typename AtomicListTraits::ListHead;

        static constexpr LinkType null_link = Traits::null_link;

        template<class Backoff, class ExternalDataOps>
        void push(LinkType node, Backoff&& backoff, ExternalDataOps&& external_data_ops) {
            auto head_ref = std::atomic_ref{*head};
            auto head_old = head_ref.load(std::memory_order_relaxed);
            while (true) {
                external_data_ops.set_next(node, head_old.next);

                auto head_new = ListHead{head_old.version + 1, node};
                if (head_ref.compare_exchange_strong(head_old, head_new, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    break;
                }
                backoff();
            }
        }

        template<class Backoff, class ExternalDataOps>
        LinkType pop(Backoff&& backoff, ExternalDataOps&& external_data_ops) {
            auto head_ref = std::atomic_ref{*head};
            auto head_old = head_ref.load(std::memory_order_relaxed);
            while (true) {
                if (head_old.next == null_link) {
                    return null_link;
                }

                auto head_new = ListHead{head_old.version + 1, external_data_ops.get_next(head_old.next)};
                if (head_ref.compare_exchange_strong(head_old, head_new, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    break;
                }
                backoff();
            }
            return head_old.next;
        }

        ListHead* head{};
    };
}