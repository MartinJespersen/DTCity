#include "mimalloc_counter_probe.hpp"
#include <mimalloc.h>
#include <cstring>

int main()
{
    // Keep a page live while recycling blocks past the 16-bit allocation count.
    // With MI_STATS=OFF, mimalloc must still reset its internal page counters.
    constexpr size_t block_count = 256;
    constexpr size_t block_size = 64;
    void* blocks[block_count] = {};
    for (size_t index = 0; index < block_count; ++index)
    {
        blocks[index] = mi_malloc(block_size);
        if (!blocks[index]) return 1;
        memset(blocks[index], static_cast<int>(index), block_size);
    }
    for (size_t iteration = 0; iteration < 1024 * 1024; ++iteration)
    {
        size_t index = iteration % block_count;
        unsigned char* bytes = static_cast<unsigned char*>(blocks[index]);
        for (size_t offset = 0; offset < block_size; ++offset)
            if (bytes[offset] != static_cast<unsigned char>(index)) return 2;
        mi_free(blocks[index]);
        blocks[index] = mi_malloc(block_size);
        if (!blocks[index]) return 1;
        memset(blocks[index], static_cast<int>(index), block_size);
    }
    for (void* block : blocks) mi_free(block);
    mi_collect(true);
    return 0;
}
