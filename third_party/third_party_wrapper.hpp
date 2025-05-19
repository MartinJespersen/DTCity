// Disable warnings for stb_image include
#pragma once

#ifdef _MSC_VER
   // Disable specific MSVC warnings
   #pragma warning(push)
   #pragma warning(disable: 4200) // disable warning about zero-sized array (if used)
   #pragma warning(disable: 4100) // disable warning about unreferenced formal parameter
   #pragma warning(disable: 4996) // disable deprecated function warnings
   #pragma warning(disable: 26812) // disable warning about unsafe functions (e.g., strcpy)

   #pragma warning(disable: 4456) // disable warning about shadowing base class member
   #pragma warning(disable: 4244) // disable warning about conversions between different types (e.g., int to float)

#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"

#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#include "meow_hash_x64_aesni.h"
#include "stb_sprintf.h"
// Re-enable warnings for your code

#ifdef _MSC_VER
    #pragma warning(pop)
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif