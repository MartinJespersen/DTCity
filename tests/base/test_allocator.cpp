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
    Allocator* allocator = Allocator::create({KB(64), KB(4), ArenaFlag_NoChain});

    TestObject* first = allocator->place<TestObject>((U32)1, log, &count);
    TestObject* second = allocator->place<TestObject>((U32)2, log, &count);

    CHECK(first->id == 1);
    CHECK(second->id == 2);

    allocator->clear();
    CHECK(count == 2);
    CHECK(log[0] == 2);
    CHECK(log[1] == 1);

    Allocator::destroy(allocator);
}
