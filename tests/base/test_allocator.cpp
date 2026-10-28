#include "../test_inc.hpp"

TEST_CASE("Allocator Runs Destructors On Clear")
{
    struct TestObject
    {
        U32 id;
        U32* log;
        U32* count;

        ~TestObject()
        {
            log[*count] = id;
            *count += 1;
        }
    };

    U32 log[2] = {};
    U32 count = 0;
    Allocator allocator_storage = Allocator::create({KB(64), KB(4), ArenaFlag_NoChain});
    Allocator* allocator = &allocator_storage;

    TestObject* first = allocator->make<TestObject>((U32)1, log, &count);
    TestObject* second = allocator->make<TestObject>((U32)2, log, &count);

    CHECK(first->id == 1);
    CHECK(second->id == 2);

    allocator->clear();
    CHECK(count == 2);
    CHECK(log[0] == 2);
    CHECK(log[1] == 1);


}

AllocatorAwareTestObject::AllocatorAwareTestObject(Allocator* allocator, U32 value, U32* destruction_count)
    : allocator(allocator), value(value), destruction_count(destruction_count)
{
}

AllocatorAwareTestObject::~AllocatorAwareTestObject()
{
    ++*destruction_count;
}

AllocatorChoiceTestObject::AllocatorChoiceTestObject(U32 value) : allocator(nullptr), value(value)
{
}

AllocatorChoiceTestObject::AllocatorChoiceTestObject(Allocator* allocator, U32 value)
    : allocator(allocator), value(value)
{
}

TEST_CASE("Allocator injects itself into both placement overloads")
{
    Allocator allocator_storage = Allocator::create();
    Allocator* allocator = &allocator_storage;

    U32 destruction_count = 0;
    auto* allocated = allocator->make<AllocatorAwareTestObject>(U32(7), &destruction_count);
    CHECK(allocated->allocator == allocator);
    CHECK(allocated->value == 7);

    alignas(AllocatorAwareTestObject) U8 storage[sizeof(AllocatorAwareTestObject)];
    auto* placed = reinterpret_cast<AllocatorAwareTestObject*>(storage);
    allocator->make(placed, U32(9), &destruction_count);
    CHECK(placed->allocator == allocator);
    CHECK(placed->value == 9);

    allocator->clear();
    CHECK(destruction_count == 2);
}

TEST_CASE("Allocator prefers injection and accepts an explicit allocator")
{
    Allocator allocator_storage = Allocator::create();
    Allocator* allocator = &allocator_storage;

    auto* injected = allocator->make<AllocatorChoiceTestObject>(U32(12));
    CHECK(injected->allocator == allocator);
    CHECK(injected->value == 12);

    auto* explicit_object = allocator->make<AllocatorChoiceTestObject>(allocator, U32(13));
    CHECK(explicit_object->allocator == allocator);
    CHECK(explicit_object->value == 13);

    U32 plain = 0;
    allocator->make(&plain, U32(14));
    CHECK(plain == 14);
}

AllocatorChoiceTestObject::AllocatorChoiceTestObject(Arena* arena, U32 value)
    : allocator(nullptr), value(value)
{
    (void)arena;
}

ArenaAwareTestObject::ArenaAwareTestObject(Arena* arena, U32 value, U32* destruction_count)
    : arena(arena), value(value), destruction_count(destruction_count)
{
}

ArenaAwareTestObject::~ArenaAwareTestObject()
{
    ++*destruction_count;
}

TEST_CASE("Allocator injects its arena into both placement overloads")
{
    Allocator allocator_storage = Allocator::create();
    Allocator* allocator = &allocator_storage;

    U32 destruction_count = 0;
    auto* allocated = allocator->make<ArenaAwareTestObject>(U32(7), &destruction_count);
    CHECK(allocated->arena == allocator->arena);
    CHECK(allocated->value == 7);

    alignas(ArenaAwareTestObject) U8 storage[sizeof(ArenaAwareTestObject)];
    auto* placed = reinterpret_cast<ArenaAwareTestObject*>(storage);
    allocator->make(placed, U32(9), &destruction_count);
    CHECK(placed->arena == allocator->arena);
    CHECK(placed->value == 9);

    auto* explicit_object = allocator->make<ArenaAwareTestObject>(allocator->arena, U32(11), &destruction_count);
    CHECK(explicit_object->arena == allocator->arena);
    CHECK(explicit_object->value == 11);

    allocator->clear();
    CHECK(destruction_count == 3);
}

TEST_CASE("Default C++ allocation uses mimalloc")
{
    U64* scalar = new U64(42);
    defer(delete scalar);
    bool scalar_owned = mi_is_in_heap_region(scalar);
    CHECK(scalar_owned);
    CHECK(*scalar == 42);

    U64* array = new U64[32]{};
    defer(delete[] array);
    bool array_owned = mi_is_in_heap_region(array);
    CHECK(array_owned);

    auto* aligned = new DynamicArrayAlignedTestItem{};
    defer(delete aligned);
    bool aligned_owned = mi_is_in_heap_region(aligned);
    CHECK(aligned_owned);
    CHECK((uintptr_t)aligned % alignof(DynamicArrayAlignedTestItem) == 0);

    U64* nothrow_value = new (std::nothrow) U64(7);
    defer(delete nothrow_value);
    REQUIRE(nothrow_value != nullptr);
    bool nothrow_owned = mi_is_in_heap_region(nothrow_value);
    CHECK(nothrow_owned);
}

ArrayMoveTestItem::~ArrayMoveTestItem()
{
    if (destruction_count)
        ++*destruction_count;
}

TEST_CASE("Allocator moves preserve registered objects and destroy them once")
{
    U32 source_destroyed = 0;
    U32 destination_destroyed = 0;
    {
        Allocator moved = Allocator::create();
        ArenaAwareTestObject* object = {};
        Arena* source_arena = {};
        {
            Allocator source = Allocator::create();
            object = source.make<ArenaAwareTestObject>(U32(42), &source_destroyed);
            source_arena = source.arena;
            Allocator constructed(std::move(source));
            CHECK(source.arena == nullptr);
            CHECK(constructed.arena == source_arena);
            moved = std::move(constructed);
            CHECK(constructed.arena == nullptr);
        }
        CHECK(source_destroyed == 0);
        CHECK(object->value == 42);
        Allocator destination = Allocator::create();
        destination.make<ArenaAwareTestObject>(U32(7), &destination_destroyed);
        destination = std::move(moved);
        CHECK(destination_destroyed == 1);
        CHECK(source_destroyed == 0);
        CHECK(destination.arena == source_arena);
        CHECK(moved.arena == nullptr);
        Allocator& alias = destination;
        destination = std::move(alias);
        CHECK(destination.arena == source_arena);
        CHECK(source_destroyed == 0);
        destination.clear();
        CHECK(source_destroyed == 1);
        destination.make<ArenaAwareTestObject>(U32(9), &source_destroyed);
    }
    CHECK(source_destroyed == 2);
    CHECK(destination_destroyed == 1);
}

TEST_CASE("Array moves transfer storage and clean up the previous destination")
{
    U32 source_destroyed = 0;
    U32 destination_destroyed = 0;
    {
        Array<ArrayMoveTestItem> destination(U32(1));
        destination[0].destruction_count = &destination_destroyed;
        {
            Array<ArrayMoveTestItem> source(U32(2));
            for (auto& item : source)
                item.destruction_count = &source_destroyed;
            ArrayMoveTestItem* original_data = source.data;
            Array<ArrayMoveTestItem> constructed(std::move(source));
            CHECK(source.data == nullptr);
            CHECK(source.size == 0);
            CHECK(constructed.data == original_data);
            destination = std::move(constructed);
            CHECK(constructed.data == nullptr);
            CHECK(constructed.size == 0);
            CHECK(destination.data == original_data);
            CHECK(destination_destroyed == 1);
            Array<ArrayMoveTestItem>& alias = destination;
            destination = std::move(alias);
            CHECK(destination.data == original_data);
            CHECK(source_destroyed == 0);
        }
        CHECK(source_destroyed == 0);
    }
    CHECK(source_destroyed == 2);
    CHECK(destination_destroyed == 1);
}

TEST_CASE("Array moves retain arena ownership and handle empty arrays")
{
    Allocator allocator = Allocator::create();
    Array<U64> source(&allocator, U32(2));
    source[0] = 17;
    U64* allocation = source.data;
    U64 allocated_position = arena_pos(allocator.arena);
    {
        Array<U64> destination(U32(0));
        destination = std::move(source);
        CHECK(destination.type == AllocationType::Arena);
        CHECK(destination.data == allocation);
        CHECK(destination[0] == 17);
        CHECK(source.data == nullptr);
    }
    U64 released_position = arena_pos(allocator.arena);
    CHECK(released_position == allocated_position);
    Array<U64> empty;
    Array<U64> moved(std::move(empty));
    CHECK(moved.data == nullptr);
    CHECK(moved.size == 0);
}

TEST_CASE("Arena arrays construct elements and destroy moved contents exactly once")
{
    Allocator allocator = Allocator::create();
    U32 destroyed = 0;
    {
        Array<ArrayMoveTestItem> source(&allocator, 3);
        for (auto& item : source)
        {
            CHECK(item.value == 37);
            item.destruction_count = &destroyed;
        }
        Array<ArrayMoveTestItem> destination(&allocator, 1);
        destination[0].destruction_count = &destroyed;
        destination = std::move(source);
        CHECK(destroyed == 1);
        CHECK(source.data == nullptr);
        CHECK(destination[2].value == 37);
    }
    CHECK(destroyed == 4);
}

TEST_CASE("Allocator destroys owned arrays before releasing their element storage")
{
    U32 destroyed = 0;
    {
        Allocator allocator = Allocator::create();
        auto* array = allocator.make<Array<ArrayMoveTestItem>>(U32(2));
        for (auto& item : *array)
        {
            CHECK(item.value == 37);
            item.destruction_count = &destroyed;
        }
    }
    CHECK(destroyed == 2);
}

TEST_CASE("Allocator chains preserve destructor order through clear reuse and move")
{
    for (ArenaFlags flags : {ArenaFlags(0), ArenaFlags(ArenaFlag_RetainCommitted)})
    {
        U32 log[12] = {};
        U32 count = 0;
        {
            Allocator allocator({KB(4), KB(4), flags});
            Arena* reused_block = 0;
            for (U64 iteration = 0; iteration < 2; ++iteration)
            {
                for (U32 id = 1; id <= 3; ++id)
                {
                    auto* object = allocator.make<ChainedAllocatorTestObject>(id, log, &count);
                    CHECK(object->id == id);
                    // Raw arena clients must also respect destructor records.
                    U8* bytes = PushArray(allocator.arena, U8, KB(4));
                    bytes[KB(4) - 1] = 42;
                }
                REQUIRE(allocator.arena->current != allocator.arena);
                if (flags && iteration) CHECK(allocator.arena->current == reused_block);
                reused_block = allocator.arena->current;
                arena_trim(allocator.arena, 0);
                allocator.clear();
                CHECK(allocator.arena->current == allocator.arena);
                CHECK(allocator.arena->destructor_count == 0);
                CHECK(count == (iteration + 1) * 3);
                CHECK(log[iteration * 3] == 3);
                CHECK(log[iteration * 3 + 1] == 2);
                CHECK(log[iteration * 3 + 2] == 1);
            }
            allocator.make<ChainedAllocatorTestObject>(U32(4), log, &count);
            PushArray(allocator.arena, U8, KB(16));
            allocator.make<ChainedAllocatorTestObject>(U32(5), log, &count);
            Allocator moved(std::move(allocator));
            Allocator destination({KB(4), KB(4), flags});
            destination = std::move(moved);
        }
        CHECK(count == 8);
        CHECK(log[6] == 5);
        CHECK(log[7] == 4);
    }
}

TEST_CASE("Allocator reserves object and destructor together at a block boundary")
{
    U32 log[2] = {};
    U32 count = 0;
    Allocator allocator({KB(4), KB(4), 0});
    Arena* root = allocator.arena;
    arena_push(root, root->res - root->pos - sizeof(ChainedAllocatorTestObject), 1);
    auto* object = allocator.make<ChainedAllocatorTestObject>(U32(1), log, &count);
    REQUIRE(root->current != root);
    CHECK(root->destructor_count == 0);
    CHECK(root->current->destructor_count == 1);
    CHECK(object->id == 1);
    // Placement construction can register a destructor even when the current block is full.
    arena_push(root, root->current->destructor_pos - root->current->pos, 1);
    alignas(ChainedAllocatorTestObject) U8 storage[sizeof(ChainedAllocatorTestObject)];
    allocator.make(reinterpret_cast<ChainedAllocatorTestObject*>(storage), U32(2), log, &count);
    allocator.clear();
    CHECK(count == 2);
    CHECK(log[0] == 2);
    CHECK(log[1] == 1);
}

TEST_CASE("Allocator keeps later blocks alive for an earlier owning destructor")
{
    U32 destroyed = 0;
    {
        Allocator allocator({KB(4), KB(4), 0});
        auto* array = allocator.make<Array<ArrayMoveTestItem>>(U32(2048));
        REQUIRE(allocator.arena->current != allocator.arena);
        REQUIRE(allocator.arena->destructor_count == 1);
        for (auto& item : *array) item.destruction_count = &destroyed;
        arena_trim(allocator.arena, 0);
    }
    CHECK(destroyed == 2048);
}

TEST_CASE("Allocator retains an explicit no-chain configuration")
{
    Allocator allocator({KB(4), KB(4), ArenaFlag_NoChain});
    CHECK((allocator.arena->flags & ArenaFlag_NoChain) != 0);
    U32 count = 0;
    allocator.make<AllocatorAwareTestObject>(U32(1), &count);
    allocator.clear();
    CHECK(count == 1);
    CHECK(allocator.arena->current == allocator.arena);
}

ChainedAllocatorTestObject::ChainedAllocatorTestObject(U32 id, U32* log, U32* count) noexcept
    : id(id), log(log), count(count)
{
}

ChainedAllocatorTestObject::~ChainedAllocatorTestObject() noexcept
{
    this->log[(*this->count)++] = this->id;
}

TEST_CASE("Allocator oversized aligned objects retain their destructor records")
{
    U32 count = 0;
    {
        Allocator allocator({KB(4), KB(4), 0});
        auto* object = allocator.make<OversizedAllocatorTestObject>(&count);
        REQUIRE(allocator.arena->current != allocator.arena);
        CHECK(IntFromPtr(object) % alignof(OversizedAllocatorTestObject) == 0);
        CHECK(allocator.arena->current->destructor_count == 1);
        CHECK(allocator.arena->current->pos <= allocator.arena->current->destructor_pos);
        arena_trim(allocator.arena, 0);
    }
    CHECK(count == 1);
}

OversizedAllocatorTestObject::OversizedAllocatorTestObject(U32* count) noexcept : count(count)
{
    this->payload[sizeof(this->payload) - 1] = 42;
}

OversizedAllocatorTestObject::~OversizedAllocatorTestObject() noexcept
{
    CHECK(this->payload[sizeof(this->payload) - 1] == 42);
    ++*this->count;
}
