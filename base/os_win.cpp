root_function u64
OS_PageSize(void)
{
 SYSTEM_INFO info;
 GetSystemInfo(&info);
 return info.dwPageSize;
}

root_function void* OS_Reserve(u64 size) {
    // Allocate memory using VirtualAlloc
    u64 gb_snapped_size = size;
    gb_snapped_size += GIGABYTE(1) - 1;
    gb_snapped_size -= gb_snapped_size%GIGABYTE(1);
    void* mappedMem = VirtualAlloc(0, size, MEM_RESERVE, PAGE_NOACCESS);
    if (mappedMem == NULL)
    {
        // Handle error if allocation fails
        exitWithError("Memory allocation failed");
    }
    return mappedMem;
}

// Function to allocate memory
root_function void OS_Alloc(void* ptr, u64 size)
{
    // Allocate memory using VirtualAlloc
    u64 page_snapped_size = size;
    page_snapped_size += OS_PageSize() - 1;
    page_snapped_size -= page_snapped_size%OS_PageSize();
    void* succes = VirtualAlloc(ptr, page_snapped_size, MEM_COMMIT, PAGE_READWRITE);
    if (!succes){
        exitWithError("failure when committing memory");
    }
}

// Function to free memory
root_function void OS_Free(void* ptr)
{
    // Free memory using VirtualFree
    if (!VirtualFree(ptr, 0, MEM_RELEASE))
    {
        // Handle error if deallocation fails
        exitWithError("Memory deallocation failed");
    }
}

root_function void OS_Release(void* ptr, u64 size) {
    if(!VirtualFree(ptr, size, MEM_DECOMMIT)){
        exitWithError("memory decommit failed");
    };
}