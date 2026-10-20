#pragma once

#include "base_lists.hpp"
#include <base/base_allocator.hpp>

template <typename T>
T&
LinkedList<T>::emplace_back(Allocator& allocator)
{
    LinkedListNode<T>* v = PushStruct(allocator.arena, LinkedListNode<T>);
    SLLQueuePush(this->first, this->last, v);
    this->count++;

    return v->v;
}

// Template implementations
template <typename T>
LinkedList<T>
singly_linked_list_copy(Arena* arena, LinkedList<T>* ll)
{
    LinkedList<T> new_ll = {};
    for (LinkedListNode<T>* node = ll->first; node; node = node->next)
    {
        LinkedListNode<T>* new_node = PushStruct(arena, T);
        SLLQueuePush(new_ll.first, new_ll.last, new_node);
    }
    return new_ll;
}
