template <typename T>
g_internal LinkedList<T>
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
