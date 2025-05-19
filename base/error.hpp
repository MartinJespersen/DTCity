#pragma once

#define ASSERT(condition, msg) assert(((void)msg, condition))

inline static void
exitWithError(const char* message)
{
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}
