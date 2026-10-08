#include "diagnostics.hpp"
#include "base/base_inc.hpp"

U64 arena_default_reserve_size = MB(64);
U64 arena_default_commit_size = KB(64);
ArenaFlags arena_default_flags = 0;

// Copyright (c) 2024 Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

//- rjf: arena creation/destruction

Arena*
arena_alloc(ArenaParams* params)
{
    // rjf: round up reserve/commit sizes
    U64 reserve_size = params->reserve_size;
    U64 commit_size = params->commit_size;
    if (params->flags & ArenaFlag_LargePages)
    {
        reserve_size = align_pow2(reserve_size, OS_GetSystemInfo()->large_page_size);
        commit_size = align_pow2(commit_size, OS_GetSystemInfo()->large_page_size);
    }
    else
    {
        reserve_size = align_pow2(reserve_size, OS_GetSystemInfo()->page_size);
        commit_size = align_pow2(commit_size, OS_GetSystemInfo()->page_size);
    }

    // rjf: reserve/commit initial block
    void* base = params->optional_backing_buffer;
    if (base == 0)
    {
        if (params->flags & ArenaFlag_LargePages)
        {
            base = os_reserve_large(reserve_size);
            os_commit_large(base, commit_size);
        }
        else
        {
            base = os_reserve(reserve_size);
            os_commit(base, commit_size);
        }
    }

    // rjf: panic on arena creation failure
#if OS_FEATURE_GRAPHICAL
    if (Unlikely(base == 0))
    {
        os_graphical_message(1, str8_lit("Fatal Allocation Failure"), str8_lit("Unexpected memory allocation failure."));
        os_abort(1);
    }
#endif

    // rjf: extract arena header & fill
    Arena* arena = (Arena*)base;
    // NOTE(mgj): Unpoison header first so we can write to it (Windows may return
    // previously-poisoned virtual addresses)
    AsanUnpoisonMemoryRegion(base, commit_size);
    arena->current = arena;
    arena->flags = params->flags;
    arena->cmt_size = commit_size;
    arena->res_size = reserve_size;
    arena->base_pos = 0;
    arena->pos = ARENA_HEADER_SIZE;
    arena->cmt = commit_size;
    arena->res = reserve_size;
    arena->destructor_pos = 0;
    arena->free_size = 0;
    arena->free_last = 0;
    Debug_ArenaCreate_Push(arena, arena->cmt);
    Debug_SetName(arena, "<unnamed>");
    return arena;
}

Arena*
arena_alloc()
{
    ArenaParams arena_params = {};
    arena_params.reserve_size = arena_default_reserve_size;
    arena_params.commit_size = arena_default_commit_size;
    arena_params.flags = arena_default_flags;
    return arena_alloc(&arena_params);
}

void
arena_release(Arena* arena)
{
    for (Arena *n = arena->free_last, *prev = 0; n != 0; n = prev)
    {
        prev = n->prev;
        Debug_ArenaRelease_Push(n);
        os_release(n, n->res);
    }
    for (Arena *n = arena->current, *prev = 0; n != 0; n = prev)
    {
        prev = n->prev;
        Debug_ArenaRelease_Push(n);
        os_release(n, n->res);
    }
}

//- rjf: arena push/pop core functions

void*
arena_push(Arena* arena, U64 size, U64 align)
{
    Arena* current = arena->current;
    U64 pos_pre = align_pow2(current->pos, align);
    U64 pos_pst = pos_pre + size;

    // rjf: chain, if needed
    if (current->res < pos_pst && !(arena->flags & ArenaFlag_NoChain))
    {
        Arena* new_block = 0;

        Arena* prev_block;
        for (new_block = arena->free_last, prev_block = 0; new_block != 0; prev_block = new_block, new_block = new_block->prev)
        {
            U64 block_start = align_pow2(ARENA_HEADER_SIZE, align);
            if (new_block->res >= block_start + size)
            {
                if (prev_block)
                {
                    prev_block->prev = new_block->prev;
                }
                else
                {
                    arena->free_last = new_block->prev;
                }
                arena->free_size -= new_block->res;
                break;
            }
        }

        if (new_block == 0)
        {
            U64 res_size = current->res_size;
            U64 cmt_size = current->cmt_size;
            U64 block_start = align_pow2(ARENA_HEADER_SIZE, align);
            if (size + block_start > res_size)
            {
                res_size = size + block_start;
                cmt_size = res_size;
            }
            ArenaParams params = {.reserve_size = res_size, .commit_size = cmt_size, .flags = current->flags};
            new_block = arena_alloc(&params);
            Debug_SetName(new_block, "arena chain block");
        }

        new_block->base_pos = current->base_pos + current->res;
        SLLStackPush_N(arena->current, new_block, prev);

        current = new_block;
        pos_pre = align_pow2(current->pos, align);
        pos_pst = pos_pre + size;
    }

    if (current->destructor_pos != 0)
    {
        AssertAlways(pos_pst <= current->destructor_pos);
    }

    // rjf: commit new pages, if needed
    if (current->cmt < pos_pst)
    {
        U64 cmt_pst_aligned = pos_pst + current->cmt_size - 1;
        cmt_pst_aligned -= cmt_pst_aligned % current->cmt_size;
        U64 cmt_pst_clamped = ClampTop(cmt_pst_aligned, current->res);
        U64 cmt_size = cmt_pst_clamped - current->cmt;
        U8* cmt_ptr = (U8*)current + current->cmt;
        if (current->flags & ArenaFlag_LargePages)
        {
            os_commit_large(cmt_ptr, cmt_size);
        }
        else
        {
            os_commit(cmt_ptr, cmt_size);
        }
        current->cmt = cmt_pst_clamped;
        Debug_PageAllocation_Push(arena, current->cmt);
    }

    // rjf: push onto current block
    void* result = 0;
    if (current->cmt >= pos_pst)
    {
        result = (U8*)current + pos_pre;
        current->pos = pos_pst;
        AsanUnpoisonMemoryRegion(result, size);
    }

    // rjf: panic on failure
#if OS_FEATURE_GRAPHICAL
    if (Unlikely(result == 0))
    {
        os_graphical_message(1, str8_lit("Fatal Allocation Failure"), str8_lit("Unexpected memory allocation failure."));
        os_abort(1);
    }
#endif

    return result;
}

U64
arena_pos(Arena* arena)
{
    Arena* current = arena->current;
    U64 pos = current->base_pos + current->pos;
    return pos;
}

void
arena_pop_to(Arena* arena, U64 pos)
{
    U64 big_pos = ClampBot(ARENA_HEADER_SIZE, pos);
    Arena* current = arena->current;

    B32 retain_committed = (arena->flags & ArenaFlag_RetainCommitted) != 0;
    B32 cache_blocks = retain_committed;
#if ARENA_FREE_LIST
    cache_blocks = true;
#endif
    for (Arena* prev = 0; current->base_pos >= big_pos; current = prev)
    {
        prev = current->prev;
        if (cache_blocks)
        {
            AsanPoisonMemoryRegion((U8*)current + ARENA_HEADER_SIZE, current->pos - ARENA_HEADER_SIZE);
            current->pos = ARENA_HEADER_SIZE;
            arena->free_size += current->res;
            SLLStackPush_N(arena->free_last, current, prev);
            if (!retain_committed)
            {
                _arena_trim_block(current, 0);
            }
        }
        else
        {
            Debug_ArenaRelease_Push(current);
            os_release(current, current->res);
        }
    }
    arena->current = current;
    U64 new_pos = big_pos - current->base_pos;
    Assert(new_pos <= current->pos);
    AsanPoisonMemoryRegion((U8*)current + new_pos, current->pos - new_pos);
    current->pos = new_pos;
    if (!retain_committed)
    {
        _arena_trim_block(current, 0);
    }
}

// Keep a bounded cache at task/idle boundaries, never inside the face loop.
void
arena_trim(Arena* arena, U64 retained_size)
{
    U64 remaining = retained_size;
    for (Arena* block = arena->current; block; block = block->prev)
    {
        _arena_trim_block(block, remaining);
        remaining -= Min(remaining, block->cmt);
    }
    Arena** link = &arena->free_last;
    while (*link)
    {
        Arena* block = *link;
        if (block->cmt <= remaining)
        {
            remaining -= block->cmt;
            link = &block->prev;
        }
        else
        {
            *link = block->prev;
            arena->free_size -= block->res;
            Debug_ArenaRelease_Push(block);
            os_release(block, block->res);
        }
    }
}

//- rjf: arena push/pop helpers

void
arena_clear(Arena* arena)
{
    arena_pop_to(arena, 0);
}

void
arena_pop(Arena* arena, U64 amt)
{
    U64 pos_old = arena_pos(arena);
    U64 pos_new = pos_old;
    if (amt < pos_old)
    {
        pos_new = pos_old - amt;
    }
    arena_pop_to(arena, pos_new);
}

//- rjf: temporary arena scopes

Temp
temp_begin(Arena* arena)
{
    U64 pos = arena_pos(arena);
    Temp temp = {arena, pos};
    return temp;
}

void
temp_end(Temp temp)
{
    arena_pop_to(temp.arena, temp.pos);
}

// Decommit only unused pages, rounded to this block's commit increment.
lib_internal void
_arena_trim_block(Arena* block, U64 retained_size)
{
    if (!(block->flags & ArenaFlag_LargePages))
    {
        U64 keep_pos = Max(block->pos, retained_size);
        keep_pos = Min(keep_pos, block->cmt);
        U64 decommit_pos = keep_pos + block->cmt_size - 1;
        decommit_pos -= decommit_pos % block->cmt_size;
        if (block->cmt > decommit_pos)
        {
            U64 decommit_size = block->cmt - decommit_pos;
            void* decommit_ptr = (U8*)block + decommit_pos;
            os_decommit(decommit_ptr, decommit_size);
            block->cmt = decommit_pos;
            Debug_PageRelease_Push(block, block->cmt);
        }
    }
}
