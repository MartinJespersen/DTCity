TEST_CASE("Add Max Item And Read Last Item")
{
    struct TestObject
    {
        U32 num;
        bool _stub;
    };

    U32 container_elem_size = 10;
    Container<TestObject>* container = container_init<TestObject>(container_elem_size);
    for (U32 i = 0; i < container_elem_size; ++i)
    {
        ContainerHandle handle = container_array_idx_get(container);
        TestObject* test_obj = container_item_from_idx(container, handle);
        test_obj->num = i;
        test_obj->_stub = false;
    }

    ContainerHandle last_obj_handle_expected = {.idx = container_elem_size};
    TestObject* last_obj = container_item_from_idx(container, last_obj_handle_expected);
    CHECK((container_elem_size - 1) == last_obj->num);
}

TEST_CASE("Add Max Items Before Overflow And Read Last Item")
{
    struct TestObject
    {
        U64 num;
        bool _stub;
    };

    U32 container_elem_size = 10;
    Container<TestObject>* container = container_init<TestObject>(container_elem_size);

    // PushStruct aligns every item to this, so the real stride is the size rounded up to it.
    U64 item_align = Max(8, AlignOf(ItemHeader<TestObject>));
    U64 item_stride = AlignPow2(sizeof(ItemHeader<TestObject>), item_align);

    // res holds the items arena + the Container struct + the nil slot; the rest is items.
    U32 max_reserved = (container->arena->res - ARENA_HEADER_SIZE - sizeof(Container<TestObject>) - item_stride) / item_stride;
    for (U32 i = 0; i < max_reserved; ++i)
    {
        ContainerHandle handle = container_array_idx_get(container);
        TestObject* test_obj = container_item_from_idx(container, handle);
        test_obj->num = i;
        test_obj->_stub = false;
    }

    ContainerHandle last_obj_handle_expected = {.idx = max_reserved};
    TestObject* last_obj = container_item_from_idx(container, last_obj_handle_expected);
    CHECK((max_reserved - 1) == last_obj->num);
}

TEST_CASE("Free item")
{
    struct TestObject
    {
        U64 num;
        bool _stub;
    };

    U32 container_elem_size = 10;
    Container<TestObject>* container = container_init<TestObject>(container_elem_size);

    ContainerHandle handle = container_array_idx_get(container);
    TestObject* test_obj = container_item_from_idx(container, handle);
    test_obj->num = 42;
    container_item_free(container, handle);

    test_obj = container_item_from_idx(container, handle);
    CHECK(test_obj->num == 0);
}
