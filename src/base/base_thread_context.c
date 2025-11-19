// Copyright (c) 2024 Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
// NOTE(allen): Thread Context Functions

static thread_static TCTX* tctx_thread_local;

static void
TCTX_InitAndEquip(TCTX* tctx)
{
    MemoryZeroStruct(tctx);
    Arena** arena_ptr = tctx->arenas;
    for (U64 i = 0; i < ArrayCount(tctx->arenas); i += 1, arena_ptr += 1)
    {
        *arena_ptr = ArenaAlloc();
    }
    Assert(!tctx->log);
    tctx->log = LogAlloc();
    Assert(!tctx_thread_local);
    tctx_thread_local = tctx;
}

static void
TCTX_Release()
{
    for (U64 i = 0; i < ArrayCount(tctx_thread_local->arenas); i += 1)
    {
        ArenaRelease(tctx_thread_local->arenas[i]);
    }
    Assert(tctx_thread_local->log);
    LogRelease(tctx_thread_local->log);
}

static TCTX*
TCTX_Get()
{
    return (tctx_thread_local);
}

static Arena*
TCTX_ScratchGet(Arena** conflicts, U64 count)
{
    TCTX* tctx = TCTX_Get();

    Arena* result = 0;
    Arena** arena_ptr = tctx->arenas;
    for (U64 i = 0; i < ArrayCount(tctx->arenas); i += 1, arena_ptr += 1)
    {
        Arena** conflict_ptr = conflicts;
        B32 has_conflict = 0;
        for (U64 j = 0; j < count; j += 1, conflict_ptr += 1)
        {
            if (*arena_ptr == *conflict_ptr)
            {
                has_conflict = 1;
                break;
            }
        }
        if (!has_conflict)
        {
            result = *arena_ptr;
            break;
        }
    }

    return (result);
}

static void
tctx_set_thread_name(String8 string)
{
    TCTX* tctx = TCTX_Get();
    U64 size = ClampTop(string.size, sizeof(tctx->thread_name));
    MemoryCopy(tctx->thread_name, string.str, size);
    tctx->thread_name_size = size;
}

static String8
tctx_get_thread_name()
{
    TCTX* tctx = TCTX_Get();
    String8 result = Str8(tctx->thread_name, tctx->thread_name_size);
    return (result);
}

static void
tctx_write_srcloc(char* file_name, U64 line_number)
{
    TCTX* tctx = TCTX_Get();
    tctx->file_name = file_name;
    tctx->line_number = line_number;
}

static void
tctx_read_srcloc(char** file_name, U64* line_number)
{
    TCTX* tctx = TCTX_Get();
    *file_name = tctx->file_name;
    *line_number = tctx->line_number;
}

////////////////////////////////
//~ mgj: Log Creation/Selection

static Log*
LogAlloc()
{
    Arena* arena = ArenaAlloc();
    Log* log = PushArray(arena, Log, 1);
    log->arena = arena;
    return log;
}

static void
LogRelease(Log* log)
{
    ArenaRelease(log->arena);
}

////////////////////////////////
//~ mgj: Log Building/Clearing

static void
LogMsg(LogMsgKind kind, String8 string)
{
    if (tctx_thread_local != 0 && tctx_thread_local->log != 0 &&
        tctx_thread_local->log->top_scope != 0)
    {
        String8 string_copy = PushStr8Copy(tctx_thread_local->log->arena, string);
        Str8ListPush(tctx_thread_local->log->arena,
                     &tctx_thread_local->log->top_scope->strings[kind], string_copy);
    }
}

static void
LogMsgF(LogMsgKind kind, char* fmt, ...)
{
    if (tctx_thread_local != 0 && tctx_thread_local->log)
    {
        Temp scratch = ScratchBegin(0, 0);
        va_list args;
        va_start(args, fmt);
        String8 string = push_str8fv(scratch.arena, fmt, args);
        LogMsg(kind, string);
        va_end(args);
        ScratchEnd(scratch);
    }
}

////////////////////////////////
//~ rjf: Log Scopes

static void
LogScopeBegin()
{
    if (tctx_thread_local != 0 && tctx_thread_local->log)
    {
        U64 pos = arena_pos(tctx_thread_local->log->arena);
        LogScope* scope = PushArray(tctx_thread_local->log->arena, LogScope, 1);
        scope->pos = pos;
        SLLStackPush(tctx_thread_local->log->top_scope, scope);
    }
}

static LogScopeResult
LogScopeEnd(Arena* arena)
{
    LogScopeResult result = {0};
    if (tctx_thread_local != 0 && tctx_thread_local->log)
    {
        LogScope* scope = tctx_thread_local->log->top_scope;
        if (scope != 0)
        {
            SLLStackPop(tctx_thread_local->log->top_scope);
            if (arena != 0)
            {
        for
            EachEnumVal(LogMsgKind, kind)
            {
                Temp scratch = ScratchBegin(&arena, 1);
                String8 result_unindented = Str8ListJoin(scratch.arena, &scope->strings[kind], 0);
                result.strings[kind] = indented_from_string(arena, result_unindented);
                ScratchEnd(scratch);
            }
            }
            ArenaPopTo(tctx_thread_local->log->arena, scope->pos);
        }
    }
    return result;
}
