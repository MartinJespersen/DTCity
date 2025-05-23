internal U64
OS_PageSize(void)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwPageSize;
}

internal void*
OS_Reserve(U64 size)
{
    // Allocate memory using VirtualAlloc
    U64 gb_snapped_size = size;
    gb_snapped_size += GIGABYTE(1) - 1;
    gb_snapped_size -= gb_snapped_size % GIGABYTE(1);
    void* mappedMem = VirtualAlloc(0, size, MEM_RESERVE, PAGE_NOACCESS);
    if (mappedMem == NULL)
    {
        // Handle error if allocation fails
        exitWithError("Memory allocation failed");
    }
    return mappedMem;
}

// Function to allocate memory
internal void
OS_Alloc(void* ptr, U64 size)
{
    // Allocate memory using VirtualAlloc
    U64 page_snapped_size = size;
    page_snapped_size += OS_PageSize() - 1;
    page_snapped_size -= page_snapped_size % OS_PageSize();
    void* succes = VirtualAlloc(ptr, page_snapped_size, MEM_COMMIT, PAGE_READWRITE);
    if (!succes)
    {
        exitWithError("failure when committing memory");
    }
}

// Function to free memory
internal void
OS_Free(void* ptr)
{
    // Free memory using VirtualFree
    if (!VirtualFree(ptr, 0, MEM_RELEASE))
    {
        // Handle error if deallocation fails
        exitWithError("Memory deallocation failed");
    }
}

internal void
OS_Release(void* ptr, U64 size)
{
    if (!VirtualFree(ptr, size, MEM_DECOMMIT))
    {
        exitWithError("memory decommit failed");
    };
}