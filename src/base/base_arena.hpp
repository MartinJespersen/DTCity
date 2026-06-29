// Copyright (c) 2024 Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef BASE_ARENA_H
#define BASE_ARENA_H

////////////////////////////////
//~ rjf: Constants

#define ARENA_HEADER_SIZE 128

////////////////////////////////
//~ rjf: Types

typedef U64 ArenaFlags;
enum
{
    ArenaFlag_NoChain = (1 << 0),
    ArenaFlag_LargePages = (1 << 1),
};

typedef struct ArenaParams ArenaParams;
struct ArenaParams
{
    U64 reserve_size;
    U64 commit_size;
    ArenaFlags flags;
    void* optional_backing_buffer;
};

typedef struct Arena Arena;
struct Arena
{
    Arena* prev;    // previous arena in chain
    Arena* current; // current arena in chain
    ArenaFlags flags;
    U64 cmt_size;
    U64 res_size;
    U64 base_pos;
    U64 pos;
    U64 cmt;
    U64 res;
    U64 destructor_pos;
#if ARENA_FREE_LIST
    U64 free_size;
    Arena* free_last;
#endif
};
StaticAssert(sizeof(Arena) <= ARENA_HEADER_SIZE, arena_header_size_check);

typedef struct Temp Temp;
struct Temp
{
    Arena* arena;
    U64 pos;
};

////////////////////////////////
//~ rjf: Global Defaults

lib_internal U64 arena_default_reserve_size = MB(64);
lib_internal U64 arena_default_commit_size = KB(64);
lib_internal ArenaFlags arena_default_flags = 0;

////////////////////////////////
//~ rjf: Arena Functions

//- rjf: arena creation/destruction

lib_internal Arena*
arena_alloc(ArenaParams* params);

lib_internal Arena*
arena_alloc();

lib_internal void
arena_release(Arena* arena);

//- rjf: arena push/pop/pos core functions
lib_internal void*
arena_push(Arena* arena, U64 size, U64 align);
lib_internal U64
arena_pos(Arena* arena);
lib_internal void
arena_pop_to(Arena* arena, U64 pos);

//- rjf: arena push/pop helpers
lib_internal void
arena_clear(Arena* arena);
lib_internal void
arena_pop(Arena* arena, U64 amt);

//- rjf: temporary arena scopes
lib_internal Temp
temp_begin(Arena* arena);
lib_internal void
temp_end(Temp temp);

//- mgj: C++ smart pointers
template <typename T>
struct ArenaRelease
{
    void
    operator()(T* value) const
    {
        arena_release(value->arena);
    }
};

template <typename T>
using ArenaUniquePtr = std::unique_ptr<T, ArenaRelease<T>>;

//- rjf: push helper macros
#define PushArrayNoZeroAligned(a, T, c, align) (T*)arena_push((a), sizeof(T) * (c), (align))                     // NOLINT(bugprone-sizeof-expression)
#define PushArrayAligned(a, T, c, align) (T*)MemoryZero(PushArrayNoZeroAligned(a, T, c, align), sizeof(T) * (c)) // NOLINT(bugprone-sizeof-expression)
#define PushArrayNoZero(a, T, c) PushArrayNoZeroAligned(a, T, c, Max(8, AlignOf(T)))
#define PushArray(a, T, c) PushArrayAligned(a, T, c, Max(8, AlignOf(T)))
#define PushStructNoZero(a, T) PushArrayNoZeroAligned(a, T, 1, Max(8, AlignOf(T)))
#define PushStruct(a, T) PushArrayAligned(a, T, 1, Max(8, AlignOf(T)))

#endif // BASE_ARENA_H
