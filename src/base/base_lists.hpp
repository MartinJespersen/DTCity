struct WriteChunk
{
    WriteChunk* next;
    Buffer<U8> buffer;
};

struct WriteChunkList
{
    WriteChunk* first;
    WriteChunk* last;
    U32 buffer_byte_count;
};

template <typename T>
struct LinkedListNode
{
    LinkedListNode* next;
    T v;
};

template <typename T>
struct LinkedList
{
    LinkedListNode<T>* first;
    LinkedListNode<T>* last;
};

template <typename T>
g_internal LinkedList<T>
singly_linked_list_copy(Arena* arena, LinkedList<T>* ll);
