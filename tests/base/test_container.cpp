TEST_CASE("Simulator field names use stable portable lookup")
{
    String8 msg_id = SIMULATION_FIELD_NAME(MsgId);
    String8 scenario_field_name = SIMULATION_FIELD_NAME(Scenarios);
    String8 stream = SIMULATION_FIELD_NAME(Stream);
    CHECK(std::string_view((char*)msg_id.str, msg_id.size) == "msg_id");
    CHECK(std::string_view((char*)scenario_field_name.str, scenario_field_name.size) == "scenarios");
    CHECK(std::string_view((char*)stream.str, stream.size) == "stream");
}

TEST_CASE("Resource pool invalidation includes its last slot and terminates the free list")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    for (U32 capacity : {1u, 3u})
    {
        ArrayResourcePool<U64> pool(arena, capacity);
        CHECK(pool.item_in_use_count == 0);
        ArrayResourcePoolHandle last = {};
        for (U32 i = 0; i < capacity; ++i)
            last = pool.handle_get();
        CHECK(pool.free_list == 0);
        pool.invalidate_all();
        bool old_valid = pool.is_handle_valid(last);
        CHECK_FALSE(old_valid);
        CHECK(pool.item_in_use_count == 0);
        for (U32 i = 0; i < capacity; ++i)
            last = pool.handle_get();
        CHECK(pool.free_list == 0);
        // Leave a link in the last slot before resetting again.
        ArrayResourcePoolHandle first = {1, pool.items[1].gen_id};
        if (capacity > 1)
            pool.item_free(first);
        pool.item_free(last);
        pool.invalidate_all();
        CHECK(pool.items[capacity].next == 0);
        for (U32 i = 0; i < capacity; ++i)
            last = pool.handle_get();
        CHECK(pool.free_list == 0);
    }
}

TEST_CASE("Array supports mutable and const iteration and indexing")
{
    Array<U64> array(U64(3));
    array[0] = 2;
    array[1] = 4;
    array[2] = 6;
    for (U64& value : array)
        value += 1;

    const Array<U64>& const_array = array;
    U64 index = 0;
    for (const U64& value : const_array)
    {
        CHECK(value == 2 * index + 3);
        CHECK(&value == &const_array[index]);
        ++index;
    }
    CHECK(index == 3);
    CHECK(array[1] == 5);
    static_assert(std::is_same_v<decltype(array[0]), U64&>);
    static_assert(std::is_same_v<decltype(const_array[0]), const U64&>);
}

TEST_CASE("Array empty ranges contain no items")
{
    Array<U64> allocated_empty(U64(0));
    Array<U64> unallocated_empty;
    U64 count = 0;
    for (U64 value : allocated_empty)
    {
        (void)value;
        ++count;
    }
    const Array<U64>& const_empty = unallocated_empty;
    for (U64 value : const_empty)
    {
        (void)value;
        ++count;
    }
    CHECK(count == 0);
    U64* first = unallocated_empty.begin();
    U64* last = unallocated_empty.end();
    CHECK(first == last);
}

TEST_CASE("DynamicArray grows from zero and preserves contiguous values")
{
    DynamicArray<U64> array = {};
    CHECK(array.size == 0);
    CHECK(array.capacity == 0);
    U64* empty_begin = array.begin();
    U64* empty_end = array.end();
    CHECK(empty_begin == empty_end);
    for (U64 value = 0; value < 4096; ++value)
    {
        U64* inserted = array.push(value);
        CHECK(*inserted == value);
    }
    REQUIRE(array.size == 4096);
    CHECK(array.capacity >= array.size);
    const DynamicArray<U64>& const_array = array;
    U64 index = 0;
    for (const U64& value : const_array)
    {
        CHECK(value == index);
        CHECK(&value == array.data + index);
        ++index;
    }
    CHECK(const_array[4095] == 4095);
    U64* allocation = array.data;
    U64 capacity = array.capacity;
    array.clear();
    CHECK(array.size == 0);
    CHECK(array.capacity == capacity);
    CHECK(array.data == allocation);
    array.push(42);
    CHECK(array[0] == 42);
}

TEST_CASE("DynamicArray supports self append across growth")
{
    DynamicArray<U32> array(1);
    array.push(7);
    array.push(array[0]);
    CHECK(array[1] == 7);
    U32 values[] = {10, 20, 30, 40, 50, 60};
    array.push(values, ArrayCount(values));
    REQUIRE(array.size == 8);
    U64 original_size = array.size;
    array.push(array.data, original_size);
    REQUIRE(array.size == 16);
    for (U64 index = 0; index < original_size; ++index)
    {
        CHECK(array[index] == array[index + original_size]);
    }
    array.push(nullptr, 0);
    CHECK(array.size == 16);
}

TEST_CASE("DynamicArray transfers ownership and can be reused after release")
{
    static_assert(!std::is_copy_constructible_v<DynamicArray<U32>>);
    static_assert(!std::is_copy_assignable_v<DynamicArray<U32>>);
    DynamicArray<U32> source(2);
    source.push(123);
    U32* allocation = source.data;
    DynamicArray<U32> moved(std::move(source));
    CHECK(source.size == 0);
    CHECK(source.capacity == 0);
    CHECK(moved.data == allocation);
    DynamicArray<U32> destination(32);
    destination = std::move(moved);
    CHECK(moved.size == 0);
    CHECK(destination[0] == 123);
    CHECK(destination.data == allocation);
    destination.release();
    destination.release();
    CHECK(destination.capacity == 0);
    destination.push(456);
    CHECK(destination[0] == 456);
    source.push(789);
    CHECK(source[0] == 789);
}

TEST_CASE("DynamicArray supports zeroed storage and over-aligned elements")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    DynamicArray<U32>* array = PushStruct(arena, DynamicArray<U32>);
    defer(array->release());
    array->push(42);
    CHECK((*array)[0] == 42);

    DynamicArray<DynamicArrayAlignedTestItem> aligned(1);
    for (U64 index = 0; index < 100; ++index)
    {
        DynamicArrayAlignedTestItem item = {index};
        DynamicArrayAlignedTestItem* inserted = aligned.push(item);
        CHECK((U64)inserted % alignof(DynamicArrayAlignedTestItem) == 0);
    }
    for (U64 index = 0; index < aligned.size; ++index)
    {
        CHECK(aligned[index].value == index);
    }
}

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
    U32 max_reserved =
        (container->arena->res - ARENA_HEADER_SIZE - sizeof(ResourcePool<TestObject>) - item_stride) / item_stride;
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

TEST_CASE("Array Resource Pool Iterates Active Items")
{
    struct TestObject
    {
        U32 num;
    };

    Arena* arena = arena_alloc();
    Debug_SetName(arena, "test container arena");
    defer(arena_release(arena));

    ArrayResourcePool<TestObject>* pool = ArrayResourcePool<TestObject>::create(arena, 4);

    U32 empty_count = 0;
    for (TestObject& object : *pool)
    {
        (void)object;
        empty_count++;
    }
    CHECK(empty_count == 0);

    ArrayResourcePoolHandle handle_a = pool->handle_get();
    ArrayResourcePoolHandle handle_b = pool->handle_get();
    ArrayResourcePoolHandle handle_c = pool->handle_get();

    TestObject* object_a = 0;
    TestObject* object_b = 0;
    TestObject* object_c = 0;
    bool object_a_found = pool->item_from_handle(handle_a, &object_a);
    bool object_b_found = pool->item_from_handle(handle_b, &object_b);
    bool object_c_found = pool->item_from_handle(handle_c, &object_c);
    CHECK(object_a_found);
    CHECK(object_b_found);
    CHECK(object_c_found);

    object_a->num = 10;
    object_b->num = 20;
    object_c->num = 30;
    pool->item_free(handle_b);

    U32 active_count = 0;
    U32 active_sum = 0;
    for (TestObject& object : *pool)
    {
        CHECK(object.num != 20);
        active_count++;
        active_sum += object.num;
        object.num += 1;
    }

    CHECK(active_count == 2);
    CHECK(active_sum == 40);
    CHECK(object_a->num == 11);
    CHECK(object_c->num == 31);
}

TEST_CASE("Map uses collision chunks and preserves value pointers")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    Map<U64, U64>* map_ptr = Map<U64, U64>::create(arena, 8);
    Map<U64, U64>& map = *map_ptr;
    U64* missing = map.get(17);
    CHECK(missing == nullptr);
    MapResult result = map.get(17, &missing);
    CHECK(result == MapResult::NotFound);
    CHECK(missing == nullptr);

    U64* first = map.insert(arena, 0, 42);
    REQUIRE(first != nullptr);
    for (U64 key = 1; key < 4096; ++key)
    {
        U64* inserted = map.insert(arena, key, key + 42);
        REQUIRE(inserted != nullptr);
    }
    U64* found = map.get(0);
    CHECK(found == first);
    CHECK(*first == 42);
    U64* duplicate = map.insert(arena, 0, 999);
    CHECK(duplicate == nullptr);
    CHECK(*first == 42);

    U64 total_count = 0;
    for (U64 bucket = 0; bucket < map.capacity; ++bucket)
    {
        auto& list = map.v[bucket];
        U64 expected_chunks = (list.total_count + DEFAULT_MAP_CHUNK_SIZE - 1) / DEFAULT_MAP_CHUNK_SIZE;
        CHECK(list.chunk_count == expected_chunks);
        total_count += list.total_count;
    }
    CHECK(total_count == 4096);
    for (U64 key = 0; key < 4096; ++key)
    {
        U64* value = {};
        result = map.get(key, &value);
        REQUIRE(result == MapResult::Success);
        CHECK(*value == key + 42);
    }

    U64 allocated_position = arena_pos(arena);
    auto* buckets = map.v;
    map.clear();
    CHECK(map.v == buckets);
    missing = map.get(0);
    CHECK(missing == nullptr);
    first = map.insert(arena, 0, 7);
    REQUIRE(first != nullptr);
    CHECK(*first == 7);
    for (U64 key = 1; key < 4096; ++key)
        map.insert(arena, key, key);
    U64 reused_position = arena_pos(arena);
    CHECK(reused_position == allocated_position);
}

TEST_CASE("Map supports explicit ownership and aligned values")
{
    using TestMap = Map<U64, DynamicArrayAlignedTestItem>;
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    TestMap* map = TestMap::create(arena, 17);
    CHECK(map->capacity == 32);
    DynamicArrayAlignedTestItem item = {};
    auto* value = map->insert(arena, 1, item);
    REQUIRE(value != nullptr);
    CHECK((uintptr_t)value % alignof(DynamicArrayAlignedTestItem) == 0);
    map->clear();
    value = map->get(1);
    CHECK(value == nullptr);
}
