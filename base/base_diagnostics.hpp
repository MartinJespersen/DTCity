#pragma once
#include <stdlib.h>

#define ASSERT(condition, msg) assert(((void)msg, condition))

inline static void
exitWithError(const char* message)
{
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

#if defined(DEBUG_BUILD)
#define DEBUG_LOG(message, ...) printf(message, __VA_ARGS__)
#else
#define DEBUG_LOG(message, ...)
#endif

#define ERROR_LOG(message, ...) fprintf(stderr, message, __VA_ARGS__)

// Push: disable all warnings
#if COMPILER_MSVC
#define DISABLE_WARNINGS_PUSH __pragma(warning(push, 0))
#elif COMPILER_CLANG
#define DISABLE_WARNINGS_PUSH                                                                      \
    _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Weverything\"")
#elif COMPILER_GCC
#define DISABLE_WARNINGS_PUSH                                                                      \
    _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wall\"")                     \
        _Pragma("GCC diagnostic ignored \"-Wextra\"")
#else
#define DISABLE_WARNINGS_PUSH
#endif

// Pop: restore previous warning state
#if COMPILER_MSVC
#define DISABLE_WARNINGS_POP __pragma(warning(pop))
#elif COMPILER_CLANG
#define DISABLE_WARNINGS_POP _Pragma("clang diagnostic pop")
#elif COMPILER_GCC
#define DISABLE_WARNINGS_POP _Pragma("GCC diagnostic pop")
#else
#define DISABLE_WARNINGS_POP
#endif
