internal U64
OS_PageSize(void);

internal void*
OS_Reserve(U64 size);

internal void
OS_Alloc(void* ptr, U64 size);

internal void
OS_Free(void* ptr);

internal void
OS_Release(void* ptr, U64 size);