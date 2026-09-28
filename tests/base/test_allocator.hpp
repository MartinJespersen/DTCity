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
