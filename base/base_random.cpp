#include <random>
static U32
RandomU32()
{
    std::random_device rd;                        // OS entropy source
    std::mt19937 gen(rd());                       // Mersenne Twister
    std::uniform_int_distribution<uint32_t> dist; // Full uint32_t range
    U32 value = dist(gen);                        // Single random value
    return value;
}
