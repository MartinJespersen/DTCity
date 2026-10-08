#pragma once

#include <memory>
#include "base_core.hpp"

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
    ArenaFlag_RetainCommitted = (1 << 2), // Reuse pages and chain blocks until explicitly trimmed.
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
    U64 free_size;
    Arena* free_last;
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

extern U64 arena_default_reserve_size;
extern U64 arena_default_commit_size;
extern ArenaFlags arena_default_flags;

////////////////////////////////
//~ rjf: Arena Functions

//- rjf: arena creation/destruction

Arena*
arena_alloc(ArenaParams* params);

Arena*
arena_alloc();

void
arena_release(Arena* arena);

//- rjf: arena push/pop/pos core functions
void*
arena_push(Arena* arena, U64 size, U64 align);
U64
arena_pos(Arena* arena);
void
arena_pop_to(Arena* arena, U64 pos);
// Trim unused commitment and cached blocks, preserving all live allocations.
// The budget includes live commitment; large-page blocks cannot be partially trimmed.
void
arena_trim(Arena* arena, U64 retained_size);

//- rjf: arena push/pop helpers
void
arena_clear(Arena* arena);
void
arena_pop(Arena* arena, U64 amt);

//- rjf: temporary arena scopes
Temp
temp_begin(Arena* arena);
void
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

lib_internal void
_arena_trim_block(Arena* block, U64 retained_size);

#endif // BASE_ARENA_H
