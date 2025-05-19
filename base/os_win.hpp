root_function u64
OS_PageSize(void);

root_function void* OS_Reserve(u64 size);

root_function void OS_Alloc(void* ptr, u64 size);

root_function void OS_Free(void* ptr);

root_function void OS_Release(void* ptr, u64 size);