#pragma once

struct PoolLifetimeItem
{
    U32* destruction_count;
    std::unique_ptr<U32> value;

    PoolLifetimeItem(U32* destruction_count, std::unique_ptr<U32> value) noexcept;
    ~PoolLifetimeItem() noexcept;
};
