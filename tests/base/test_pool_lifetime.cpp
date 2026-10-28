#include "test_pool_lifetime.hpp"

PoolLifetimeItem::PoolLifetimeItem(U32* destruction_count, std::unique_ptr<U32> value) noexcept
    : destruction_count(destruction_count), value(std::move(value))
{
}

PoolLifetimeItem::~PoolLifetimeItem() noexcept
{
    ++*this->destruction_count;
}

TEST_CASE("Pool emplace forwards ownership and destroys each live object once")
{
    Allocator allocator = Allocator::create();
    U32 destroyed = 0;
    {
        ResourcePool<PoolLifetimeItem> pool(allocator.arena, 2);
        auto value = std::make_unique<U32>(42);
        PoolHandle first = pool.emplace(&destroyed, std::move(value));
        CHECK_FALSE(value);
        PoolLifetimeItem* item = {};
        REQUIRE(pool.item_from_handle(first, &item));
        CHECK(*item->value == 42);
        pool.item_free(first);
        CHECK(destroyed == 1);
        pool.item_free(first);
        CHECK(destroyed == 1);
        PoolHandle reused = pool.emplace(&destroyed, std::make_unique<U32>(7));
        CHECK(reused.idx == first.idx);
        CHECK(reused.gen_id != first.gen_id);
        CHECK_FALSE(pool.is_handle_valid(first));
        pool.emplace(&destroyed, std::make_unique<U32>(8));
        pool.invalidate_all();
        CHECK(destroyed == 3);
        CHECK(pool.item_in_use_count == 0);
        pool.invalidate_all();
        CHECK(destroyed == 3);
        pool.emplace(&destroyed, std::make_unique<U32>(9));
    }
    CHECK(destroyed == 4);
}

TEST_CASE("Pool cleanup does not destroy unconstructed raw reservations")
{
    Allocator allocator = Allocator::create();
    ResourcePool<PoolLifetimeItem> pool(allocator.arena, 2);
    PoolHandle first = pool.handle_get();
    PoolLifetimeItem* raw = {};
    pool.item_new(&raw);
    pool.item_free(first);
    pool.invalidate_all();
    CHECK(pool.item_in_use_count == 0);
}
