// Arenas
root_function Arena*
ArenaAlloc(u64 size)
{
    u64 headerSize = sizeof(Arena);
    u64 cmtSize = OS_PageSize();
    void* block = OS_Reserve(size);
    OS_Alloc(block, cmtSize);
    AsanPoisonMemoryRegion(block, cmtSize);
    AsanUnpoisonMemoryRegion(block, headerSize);

    Arena* arena = (Arena*)block;
    arena->resSize = size;
    arena->pos = headerSize;
    arena->cmtSize = cmtSize;
    arena->cmt = cmtSize;
    arena->align = 8;

    return arena;
}

root_function void*
ArenaPushAlign(Arena* arena, u64 size, u64 align)
{
    u64 posPre = arena->pos;
    arena->pos += (align - 1);
    arena->pos -= arena->pos % align;
    u64 posPost = arena->pos + size;

    if (posPost > arena->resSize)
    {
        exitWithError("Arena Error: Not enough space reserved");
    }

    // commit memory in new block
    if (posPost > arena->cmt) {
        u64 cmtNew = posPost + arena->cmtSize - 1;
        cmtNew -= cmtNew%arena->cmtSize;
        u64 cmtNewClamped = Min(cmtNew, arena->resSize);
        u64 cmtSize = cmtNewClamped - arena->cmt;
        void* cmtPtr = (void*)((u8*)arena + arena->cmt);
        OS_Alloc(cmtPtr, cmtSize);
        arena->cmt = cmtNewClamped;
        AsanPoisonMemoryRegion((u8*)arena+posPost, (arena->cmt-posPost));
    }

    // unpoison memory
    AsanUnpoisonMemoryRegion((u8*)arena+posPre, (posPost - posPre));
    
    void* result = (void*)((u8*)arena + arena->pos);
    arena->pos = posPost;
    return result;
}

root_function void*
ArenaPush(Arena* arena, u64 size)
{
    return ArenaPushAlign(arena, size, arena->align);
}

root_function void*
ArenaPushZero(Arena* arena, u64 size)
{
    void* box = ArenaPush(arena, size);
    MemoryZero(box, size);
    return box;
}

root_function void*
ArenaPushZeroAlign(Arena* arena, u64 size, u64 align)
{
    void* box = ArenaPushAlign(arena, size, align);
    MemoryZero(box, size);
    return box;
}

root_function void
ArenaPop(Arena* arena, u64 pos)
{
    ASSERT(pos<=arena->pos, "ArenaPop: Input position should always be below the current position"); 
    //uncommit memory
    u64 unCmtLimit = arena->cmt - arena->cmtSize;
    if(pos < unCmtLimit) {
        u64 posCmt = pos + arena->cmtSize - 1;
        posCmt -= posCmt%arena->cmtSize;
        u64 unCmtSize = arena->cmt - posCmt;
        void* posCmtPtr = (void*)((u8*)arena + posCmt);
        OS_Release(posCmtPtr, unCmtSize);
        arena->cmt = posCmt;
    }

    AsanPoisonMemoryRegion((u8*)arena+pos, (arena->pos - pos));
    arena->pos = pos;
}

root_function void
ArenaReset(Arena* arena) {
    ArenaPop(arena, sizeof(Arena));
}

root_function void
ArenaDealloc(Arena* arena)
{
    OS_Free((void*)arena);
}

root_function ArenaTemp
ArenaTempBegin(Arena* arena)
{
    ArenaTemp arenaTemp = {arena, arena->pos};
    return arenaTemp;
}

root_function void
ArenaTempEnd(ArenaTemp temp)
{
    ArenaPop(temp.arena, temp.pos);
}

// String functions

root_function String8
Str8(u8* str, u64 size)
{
    String8 str8 = {};
    str8.size = size;
    str8.str = str;
    return str8;
}

root_function String8
Str8Push(Arena* arena, String8 str)
{
    String8 target = {};
    target.size = str.size;
    target.str = PushArray(arena, u8, target.size + 1); // +1 for space for null termination
    MemoryCopy(target.str, str.str, str.size);
    return target;
}

root_function String8
Str8NullTermFromStr8(String8 str)
{
    str.str[str.size] = (u8)'\0';
    return str;
}

root_function u8*
StrFromU128(Arena* arena, u128 num)
{
    u64 size = sizeof(u128);
    u8* str = PushArray(arena, u8, size + 1);
    for (u32 numI = 0; numI < ArrayCount(num.data); numI++)
    {
        for (u32 i = 0; i < sizeof(u64); i++)
        {
            str[numI * sizeof(u64) + i] = (u8)(num.data[numI] >> (8 * i));
        }
    }
    str[size] = '\0';

    return str;
}

root_function String8
Str8V(Arena* arena, const char* str, va_list args)
{
    String8 result = {0};
    va_list args2;
    va_copy(args2, args);
    u64 sizeWithNullTermination = (u64)stbsp_vsnprintf(0, 0, str, args) + 1UL;
    result.size = sizeWithNullTermination - 1;

    result.str = PushArray(arena, u8, sizeWithNullTermination);
    stbsp_vsnprintf((char*)result.str, (i32)sizeWithNullTermination, str, args2);

    return result;
}

root_function String8
Str8(Arena* arena, const char* str, ...)
{
    va_list args;
    va_start(args, str);
    String8 result = Str8V(arena, str, args);
    va_end(args);
    return result;
}

root_function char*
StrFromStr8(String8 str)
{
    return (char*)str.str;
}

root_function char**
StrArrFromStr8Buffer(Arena* arena, String8* buffer, u64 count)
{
    char** arr = PushArray(arena, char*, count);

    for (u32 i = 0; i < count; i++)
    {
        arr[i] = (char*)buffer[i].str;
    }
    return arr;
}

// Buffers
BufferImpl(u16);
BufferImpl(String8);

// helpers
template <typename N>
root_function N
Clamp(N d, N min, N max)
{
    N temp = d > min ? d : min;
    return temp < max ? temp : max;
}