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
        U64 idx = container_array_idx_get(container);
        TestObject* test_obj = container_item_from_idx(container, idx);
        test_obj->num = i;
        test_obj->_stub = false;
    }

    TestObject* last_obj = container_item_from_idx(container, container_elem_size - 1);
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
    U64 max_reserved = (container->arena->res - ARENA_HEADER_SIZE - sizeof(Container<TestObject>) - item_stride) / item_stride;
    for (U32 i = 0; i < max_reserved; ++i)
    {
        U64 idx = container_array_idx_get(container);
        TestObject* test_obj = container_item_from_idx(container, idx);
        test_obj->num = i;
        test_obj->_stub = false;
    }

    TestObject* last_obj = container_item_from_idx(container, max_reserved - 1);
    CHECK((max_reserved - 1) == last_obj->num);
}
