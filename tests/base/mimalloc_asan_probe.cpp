#include "mimalloc_asan_probe.hpp"
// Standalone executable: define the replacement operators exactly once.
#include <mimalloc-new-delete.h>

int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;

    char* allocation = new char[64];
    bool owned = mi_is_in_heap_region(allocation);
    if (!owned)
        return 3;

    volatile char* access = allocation;
    if (strcmp(argv[1], "overflow") == 0)
    {
        access[64] = 1;
        return 4;
    }
    if (strcmp(argv[1], "use_after_free") == 0)
    {
        delete[] allocation;
        access[32] = 1;
        return 5;
    }

    access[63] = 7;
    bool accessible = !__asan_address_is_poisoned(allocation + 63);
    bool boundary_poisoned = __asan_address_is_poisoned(allocation + 64);
    delete[] allocation;
    bool freed_poisoned = __asan_address_is_poisoned(access + 32);
    if (!accessible || !boundary_poisoned || !freed_poisoned)
        return 6;

    // Cover size-class rounding, page allocation, and large allocations.
    const size_t sizes[] = {1, 7, 8, 63, 65, 4096, 131072, 1048576};
    for (size_t size : sizes)
    {
        char* bytes = new char[size]{};
        volatile char* writable = bytes;
        writable[size - 1] = 1;
        bool boundary = __asan_address_is_poisoned(bytes + size);
        delete[] bytes;
        if (!boundary)
            return 8;
    }

    auto* aligned = new MimallocProbeAligned{};
    bool aligned_owned = mi_is_in_heap_region(aligned);
    aligned->bytes[63] = 7;
    delete aligned;
    int* scalar = new (std::nothrow) int(42);
    bool scalar_owned = mi_is_in_heap_region(scalar);
    delete scalar;
    return aligned_owned && scalar_owned ? 0 : 7;
}
