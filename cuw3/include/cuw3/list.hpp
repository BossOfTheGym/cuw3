#pragma once

#include <algorithm>

namespace cuw3 {
    // Some forethought:
    // * we would like to use pointer compression.
    // * next and prev are no longer pointers: they are numerical handles.
    // * we would like to encapsulate everything list-related (prev and next) inside a struct (entry struct)
    // * list entry will then be embedded inside some object.
    // * we don't want to shitcode null-checks inside our precious algorithm, we would like to use terminator-node instead.
    //   * that is, our 'head' (terminator-node) will contain references to true head and tail.
    // * but terminator-node, in general, is not a complete object - it is just an entry!
    //   * that is, NodeRef abstraction must handle terminator-node case as well.
    // * we would like to use plain pointers too if we feel like it.
    //   * in this case NodeRef may just store a pointer to an entry (no handle required, handle is equivalent to pointer to an entry).
    // * usage of compressed pointers is useful as it can make our datastructure fully relocatable 

    // Description of the abstraction 'ListOps'
    // * encapsulates some context required to translate numerical handles back into the objects
    // * encapsulates operations required to manipulate entries and to retrieve entries
    // * such encapsulation allows the algorithm to not care about NodeRef structure at all
    // * must contain the following operations:
    //   * node_ref get_prev(node_ref)
    //   * void set_prev(node_ref, prev_ref)
    //   * node_ref get_next(node_ref)
    //   * void set_next(node_ref, next_ref)
    //   * bool ref_equals(node1_ref, node2_ref)
    //   * bool self_equals(node_ref)
    // * that's it
    // * ops must be a light-weight object

    // Careful while moving list! Especially, if terminator node is not dynamically allocated: pointers of the next and prev nodes must be reassigned

    // NOTE : despite the fact that terminator-node cannot be in general considered head of the list...
    //   It will be called list ... as it holds the list!

    // Description of the abstraction 'NodeRef'
    // * implementation looks like super-handle, can be used to retrieve both numeric handle and pointer to an object.
    // * this abstraction allows us to transparently use pointer compression (use numeric handle that can be smaller in size than full 64-bit value).
    // * handle can be equivalent to an entry pointer. it can happen in case when we dont use pointer compression.
    // * no need to store any handle besides pointer to the entry in this case.
    // * fields of the NodeRef are never directly accessed in any of the algorithms. all manipulations are done via list ops. 
    // * usage of handles instead of pointers can be useful if we want to make objects relocatable
    // * structure of NodeRef is hidden from the algorithm
    // * ref must be a light-weight object

    // Examples
    // * check for an example of list using pointer compression
    // * check for an example of list without pointer compression

    // node will reference itself
    // can be used to initialize a list (terminator-node)
    template<class NodeRef, class ListOps>
    void list_init(NodeRef node, ListOps&& ops) {
        ops.set_prev(node, node);
        ops.set_next(node, node);
    }

    template<class NodeRef, class ListOps>
    auto list_prev(NodeRef node, ListOps&& ops) {
        return ops.get_prev(node);
    }

    template<class NodeRef, class ListOps>
    auto list_next(NodeRef node, ListOps&& ops) {
        return ops.get_next(node);
    }

    // checks that node references itself
    // can be used toi check that list is empty (only terminator-node is in the list)
    template<class NodeRef, class ListOps>
    bool list_empty(NodeRef node, ListOps&& ops) {
        return ops.self_equals(node);
    }

    template<class NodeRef, class ListOps>
    void list_insert_after(NodeRef after, NodeRef node, ListOps&& ops) {
        // standart implementation
        // node->next = after->next;
        // node->prev = after;
        // after->next->prev = node;
        // after->next = node;

        auto after_next = ops.get_next(after);
        ops.set_prev(node, after);
        ops.set_next(node, after_next);
        ops.set_prev(after_next, node);
        ops.set_next(after, node);
    }

    template<class NodeRef, class ListOps>
    void list_insert_before(NodeRef before, NodeRef node, ListOps&& ops) {
        list_insert_after(ops.get_prev(before), node, ops);
    }

    // be careful if you erase terminator-node from the list
    // the rest of the list would just dangle
    template<class NodeRef, class ListOps>
    void list_erase(NodeRef node, ListOps&& ops) {
        // standart implementation
        // Entry* prev = entry->prev;
        // Entry* next = entry->next;
        // prev->next = next;
        // next->prev = prev;

        auto prev = ops.get_prev(node);
        auto next = ops.get_next(node);
        ops.set_next(prev, next);
        ops.set_prev(next, prev);
    }

    // alias
    template<class NodeRef, class ListOps>
    void list_push_head(NodeRef list, NodeRef node, ListOps&& ops) {
        list_insert_after(list, node, ops);
    }

    // alias
    template<class NodeRef, class ListOps>
    void list_push_tail(NodeRef list, NodeRef node, ListOps&& ops) {
        list_insert_before(list, node, ops);
    }

    // tolerates empty list
    // in this case it will return reference to the node passed into the function
    template<class NodeRef, class ListOps>
    auto list_pop_head(NodeRef list, ListOps&& ops) {
        auto popped = ops.get_next(list);
        list_erase(popped, ops);
        return popped;
    }

    // tolerates empty list
    // in this case it will return reference to the node passed into the function
    template<class NodeRef, class ListOps>
    auto list_pop_tail(NodeRef list, ListOps&& ops) {
        auto popped = ops.get_prev(list);
        list_erase(popped, ops);
        return popped;
    }

    template<class Node>
    struct DefaultListOps {
        Node* get_prev(Node* node) {
            return node->prev;
        }

        void set_prev(Node* node, Node* prev) {
            node->prev = prev;
        }

        Node* get_next(Node* node) {
            return node->next;
        }

        void set_next(Node* node, Node* next) {
            node->next = next;
        }

        bool ref_equals(Node* node1, Node* node2) {
            return node1 == node2;
        }

        bool self_equals(Node* node) {
            return node->prev == node && node->next == node;
        }
    };
}