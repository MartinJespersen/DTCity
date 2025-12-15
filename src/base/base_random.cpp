#include <random>

g_internal U32
RandomU32()
{
    std::random_device rd;                   // OS entropy source
    std::mt19937 gen(rd());                  // Mersenne Twister
    std::uniform_int_distribution<U32> dist; // Full uint32_t range
    U32 value = dist(gen);                   // Single random value
    return value;
}

g_internal U64
random_u64()
{
    std::random_device rd;                   // OS entropy source
    std::mt19937 gen(rd());                  // Mersenne Twister
    std::uniform_int_distribution<U64> dist; // Full uint32_t range
    U64 value = dist(gen);                   // Single random value
    return value;
}
