TEST_CASE("Freelist creation with buffer_handle lifecycle")
{
    U32 buffer_size = 10;
    U32 test_value = 11;
    ScratchScope scratch = ScratchScope(0, 0);
    Pow2Freelist* freelist = pow2_freelist_create(scratch.arena);

    BufferHandle<U32> buffer_handle = buffer_from_pow2_freelist<U32>(scratch.arena, freelist, buffer_size);
    Buffer<U32> buffer = {};
    REQUIRE(buffer_handle.buffer_try_get(&buffer));
    CHECK(buffer.size == buffer_size);
    CHECK(buffer.data != nullptr);
    buffer.data[buffer_size - 1] = test_value;

    CHECK(buffer_push_to_freelist(freelist, buffer_handle));

    CHECK(buffer_handle.buffer_try_get(&buffer) == false);
    CHECK(buffer_handle.gen_id == 0);

    U32 offset = _pow2_freelist_offset_calc<U32>();
    U64 alloc_size = _pow2_freelist_alloc_size_find(buffer_size * sizeof(U32), offset, freelist->exponent_min);
    U32 idx = _pow2_freelist_idx_from_alloc_size(alloc_size, freelist->exponent_min, freelist->exponent_count);

    Pow2FreelistNode* node = freelist->free_list[idx];
    CHECK(node != nullptr);
    CHECK(buffer.data[buffer_size - 1] == test_value);
    CHECK(node->gen_id == 2);
    CHECK(buffer_handle.gen_id == 0);

    BufferHandle<U32> reused_handle = buffer_from_pow2_freelist<U32>(scratch.arena, freelist, buffer_size);
    Buffer<U32> reused_buffer = {};
    REQUIRE(reused_handle.buffer_try_get(&reused_buffer));
    CHECK(reused_buffer.data == buffer.data);
    CHECK(reused_buffer.data[buffer_size - 1] == 0);
    CHECK(reused_handle.gen_id == 2);
}

TEST_CASE("Freelist allocates enough bytes for large elements")
{
    struct TestElement
    {
        U64 values[8];
    };

    U32 buffer_size = 1024;
    ScratchScope scratch = ScratchScope(0, 0);
    Pow2Freelist* freelist = pow2_freelist_create(scratch.arena);

    BufferHandle<TestElement> buffer_handle = buffer_from_pow2_freelist<TestElement>(scratch.arena, freelist, buffer_size);
    Buffer<TestElement> buffer = {};
    REQUIRE(buffer_handle.buffer_try_get(&buffer));

    for (U32 element_idx = 0; element_idx < buffer_size; ++element_idx)
    {
        for (U32 value_idx = 0; value_idx < ArrayCount(buffer.data[element_idx].values); ++value_idx)
        {
            buffer.data[element_idx].values[value_idx] = element_idx + value_idx;
        }
    }

    U32 last_value_idx = ArrayCount(buffer.data[buffer_size - 1].values) - 1;
    U64 expected_value = buffer_size + last_value_idx - 1;
    CHECK(buffer.data[buffer_size - 1].values[last_value_idx] == expected_value);
}
