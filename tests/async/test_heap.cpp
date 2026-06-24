TEST_CASE("async min heap keeps the smallest key at the top")
{
    async::Heap<S64>* heap = async::async_heap_alloc<S64>();
    async::HeapItem<S64> item = {};

    async::async_min_heap_push(heap, (S64)20, 20);
    async::async_min_heap_push(heap, (S64)5, 5);
    async::async_min_heap_push(heap, (S64)10, 10);

    CHECK(async::async_min_heap_peek(heap, &item));
    CHECK(item.k == 5);
    CHECK(item.v == 5);

    async::async_min_heap_pop(heap);
    CHECK(async::async_min_heap_peek(heap, &item));
    CHECK(item.k == 10);
    CHECK(item.v == 10);

    async::async_min_heap_pop(heap);
    CHECK(async::async_min_heap_peek(heap, &item));
    CHECK(item.k == 20);
    CHECK(item.v == 20);

    async::async_heap_release(heap);
}

TEST_CASE("async min heap handles single item and empty pop")
{
    async::Heap<S32>* heap = async::async_heap_alloc<S32>();
    async::HeapItem<S32> item = {};

    async::async_min_heap_pop(heap);
    CHECK(heap->size == 0);

    async::async_min_heap_push(heap, (S32)7, 7);
    CHECK(async::async_min_heap_peek(heap, &item));
    CHECK(item.k == 7);
    CHECK(item.v == 7);

    async::async_min_heap_pop(heap);
    CHECK(heap->size == 0);

    async::async_heap_release(heap);
}

TEST_CASE("async min heap peek returns false when heap is empty")
{
    async::Heap<S32>* heap = async::async_heap_alloc<S32>();
    async::HeapItem<S32> item = {.k = 17, .v = 17};

    CHECK(heap->size == 0);
    CHECK_FALSE(async::async_min_heap_peek(heap, &item));
    CHECK(item.k == 17);
    CHECK(item.v == 17);

    async::async_min_heap_push(heap, (S32)7, 7);
    CHECK(async::async_min_heap_peek(heap, &item));
    CHECK(item.k == 7);
    CHECK(item.v == 7);

    async::async_heap_release(heap);
}
