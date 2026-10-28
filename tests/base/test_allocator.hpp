#pragma once

struct AllocatorAwareTestObject
{
    Allocator* allocator;
    U32 value;
    U32* destruction_count;

    AllocatorAwareTestObject(Allocator* allocator, U32 value, U32* destruction_count);
    ~AllocatorAwareTestObject();
};

struct AllocatorChoiceTestObject
{
    Allocator* allocator;
    U32 value;

    explicit AllocatorChoiceTestObject(U32 value);
    AllocatorChoiceTestObject(Allocator* allocator, U32 value);
    AllocatorChoiceTestObject(Arena* arena, U32 value);
};

struct ArenaAwareTestObject
{
    Arena* arena;
    U32 value;
    U32* destruction_count;

    ArenaAwareTestObject(Arena* arena, U32 value, U32* destruction_count);
    ~ArenaAwareTestObject();
};

struct ArrayMoveTestItem
{
    U32* destruction_count = {};
    U32 value = 37;
    ~ArrayMoveTestItem();
};

struct ChainedAllocatorTestObject
{
    U32 id;
    U32* log;
    U32* count;

    ChainedAllocatorTestObject(U32 id, U32* log, U32* count) noexcept;
    ~ChainedAllocatorTestObject() noexcept;
};

struct alignas(64) OversizedAllocatorTestObject
{
    U8 payload[KB(8)];
    U32* count;

    explicit OversizedAllocatorTestObject(U32* count) noexcept;
    ~OversizedAllocatorTestObject() noexcept;
};
