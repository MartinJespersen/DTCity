#pragma once

struct TaskPayloadLifetimeProbe
{
    U32* destruction_count;
    U32* allocator_destruction_count;

    ~TaskPayloadLifetimeProbe() noexcept;
};
