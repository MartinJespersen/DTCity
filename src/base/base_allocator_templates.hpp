#pragma once

#include "base_allocator.hpp"

// Template implementations
template <typename T>
T
Allocator::create(ArenaParams params) noexcept
{
    return T{Allocator(params)};
}

template <typename T, typename... Args>
T*
Allocator::_allocator_construct(void* mem, Args&&... args)
{
    // Prefer Allocator*, then Arena*, using the same brace initialization as make().
    if constexpr (requires { T{this, std::forward<Args>(args)...}; })
    {
        return new (mem) T{this, std::forward<Args>(args)...};
    }
    else if constexpr (requires { T{arena, std::forward<Args>(args)...}; })
    {
        return new (mem) T{arena, std::forward<Args>(args)...};
    }
    else
    {
        return new (mem) T{std::forward<Args>(args)...};
    }
}

