TEST_CASE("Add Max Item And Read Last Item")
{
    struct TestObject
    {
        U32 num;
        bool _stub;
    };

    U32 container_elem_size = 10;
    ResourcePool<TestObject>* container = resource_pool_init<TestObject>(container_elem_size);
    for (U32 i = 0; i < container_elem_size; ++i)
    {
        ResourcePoolHandle handle = resource_pool_array_idx_get(container);
        TestObject* test_obj = resource_pool_item_from_idx(container, handle);
        test_obj->num = i;
        test_obj->_stub = false;
    }

    ResourcePoolHandle last_obj_handle_expected = {.idx = container_elem_size};
    TestObject* last_obj = resource_pool_item_from_idx(container, last_obj_handle_expected);
    CHECK((container_elem_size - 1) == last_obj->num);
    resource_pool_release(container);
}

TEST_CASE("Add Max Items Before Overflow And Read Last Item")
{
    struct TestObject
    {
        U64 num;
        bool _stub;
    };

    U32 container_elem_size = 10;
    ResourcePool<TestObject>* container = resource_pool_init<TestObject>(container_elem_size);

    // PushStruct aligns every item to this, so the real stride is the size rounded up to it.
    U64 item_align = Max(8, AlignOf(ItemHeader<TestObject>));
    U64 item_stride = align_pow2(sizeof(ItemHeader<TestObject>), item_align);

    // res holds the items arena + the ResourcePool struct + the nil slot; the rest is items.
    U32 max_reserved = (container->arena->res - ARENA_HEADER_SIZE - sizeof(ResourcePool<TestObject>) - item_stride) / item_stride;
    for (U32 i = 0; i < max_reserved; ++i)
    {
        ResourcePoolHandle handle = resource_pool_array_idx_get(container);
        TestObject* test_obj = resource_pool_item_from_idx(container, handle);
        test_obj->num = i;
        test_obj->_stub = false;
    }

    ResourcePoolHandle last_obj_handle_expected = {.idx = max_reserved};
    TestObject* last_obj = resource_pool_item_from_idx(container, last_obj_handle_expected);
    CHECK((max_reserved - 1) == last_obj->num);
    resource_pool_release(container);
}

TEST_CASE("Free item")
{
    struct TestObject
    {
        U64 num;
        bool _stub;
    };

    U32 container_elem_size = 10;
    ResourcePool<TestObject>* container = resource_pool_init<TestObject>(container_elem_size);

    ResourcePoolHandle handle = resource_pool_array_idx_get(container);
    TestObject* test_obj = resource_pool_item_from_idx(container, handle);
    test_obj->num = 42;
    resource_pool_item_free(container, handle);

    test_obj = resource_pool_item_from_idx(container, handle);
    CHECK(test_obj->num == 0);
    resource_pool_release(container);
}

TEST_CASE("Array Resource Pool Reuses Freed Slot")
{
    struct TestObject
    {
        U32 num;
    };

    Arena* arena = arena_alloc();
    Debug_SetName(arena, "test container arena");
    defer(arena_release(arena));

    ArrayResourcePool<TestObject>* pool = ArrayResourcePool<TestObject>::create(arena, 2);

    ArrayResourcePoolHandle handle_a = pool->handle_get();
    ArrayResourcePoolHandle handle_b = pool->handle_get();

    CHECK(handle_a.idx == 1);
    CHECK(handle_b.idx == 2);
    CHECK(pool->free_list == 0);

    TestObject* object_a = 0;
    bool object_a_found = pool->item_from_handle(handle_a, &object_a);
    CHECK(object_a_found);
    object_a->num = 42;

    pool->item_free(handle_a);

    object_a_found = pool->item_from_handle(handle_a, &object_a);
    CHECK(!object_a_found);

    ArrayResourcePoolHandle handle_c = pool->handle_get();
    CHECK(handle_c.idx == handle_a.idx);
    CHECK(handle_c.gen_id != handle_a.gen_id);

    object_a_found = pool->item_from_handle(handle_c, &object_a);
    CHECK(object_a_found);
    CHECK(object_a->num == 42);
}

TEST_CASE("Dynamic Array Grows")
{
    dynamic_array_init(MB(1));

    DynamicArray<U32> array = dynamic_array_create<U32>(1);
    U32 values[128] = {};
    for (U32 i = 0; i < ArrayCount(values); i += 1)
    {
        values[i] = i + 1;
    }
    Buffer<U32> value_buffer = {.data = values, .size = ArrayCount(values)};
    dynamic_array_append(&array, value_buffer);

    CHECK(array.size == ArrayCount(values));
    CHECK(array.capacity >= ArrayCount(values));
    CHECK(array.data[0] == 1);
    CHECK(array.data[1] == 2);
    CHECK(array.data[127] == 128);

    dynamic_array_destroy(&array);
    dynamic_array_release();
}
