#pragma once

#include "base/base_inc.hpp"
#include "base/base_allocator_templates.hpp"

struct ArenaFailureTestState
{
    B32 fail_reserve;
    B32 fail_commit;
    U64 mappings;
    U64 stack_reports;
};

struct ArenaFailureTestAbort
{
    S32 exit_code;
};

struct ArenaFailureTestObject
{
    ~ArenaFailureTestObject();
};
