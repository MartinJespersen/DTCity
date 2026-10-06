#pragma once

// Copyright (c) 2024 Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef BASE_THREAD_CONTEXT_H
#define BASE_THREAD_CONTEXT_H
////////////////////////////////
//~ rjf: Log Types
typedef enum LogMsgKind
{
    LogMsgKind_Info,
    LogMsgKind_UserError,
    LogMsgKind_COUNT
} LogMsgKind;

typedef struct LogScope LogScope;
struct LogScope
{
    LogScope* next;
    U64 pos;
    String8List strings[LogMsgKind_COUNT];
};

typedef struct LogScopeResult LogScopeResult;
struct LogScopeResult
{
    String8 strings[LogMsgKind_COUNT];
};

typedef struct Log Log;
struct Log
{
    Arena* arena;
    LogScope* top_scope;
};

////////////////////////////////
//~ mgj: Log Creation/Selection

Log*
LogAlloc();
void
LogRelease(Log* log);

void
LogMsg(LogMsgKind kind, String8 string);
void
LogMsgF(LogMsgKind kind, char* fmt, ...);
#define LogInfo(s) LogMsg(LogMsgKind_Info, (s))
#define LogInfoF(...) LogMsgF(LogMsgKind_Info, __VA_ARGS__)
#define log_user_error(s) LogMsg(LogMsgKind_UserError, (s))
#define log_user_errorf(...) LogMsgF(LogMsgKind_UserError, __VA_ARGS__)

#define LogInfoNamedBlock(s) DeferLoop(LogInfoF("%s:\n{\n", ((s).str)), LogInfoF("}\n"))
#define LogInfoNamedBlockF(...) DeferLoop((LogInfoF(__VA_ARGS__), LogInfoF(":\n{\n")), LogInfoF("}\n"))

void
LogScopeBegin();
LogScopeResult
LogScopeEnd(Arena* arena);

////////////////////////////////
//~ rjf: Lane Context

typedef struct LaneCtx LaneCtx;
struct LaneCtx
{
    U64 lane_idx;
    U64 lane_count;
    Barrier barrier;
    U64* broadcast_memory;
};
////////////////////////////////
// NOTE(allen): Thread Context

typedef struct TCTX TCTX;
struct TCTX
{
    Arena* arenas[2];

    U8 thread_name[32];
    U64 thread_name_size;

    char* file_name;
    U64 line_number;

    LaneCtx lane_ctx;

    Log* log;
};

////////////////////////////////
// NOTE(allen): Thread Context Functions

void
TCTX_InitAndEquip(TCTX* tctx);
void
TCTX_Release();
TCTX*
TCTX_Get();

Arena*
TCTX_ScratchGet(Arena** conflicts, U64 countt);

void
tctx_set_thread_name(String8 name);
String8
tctx_get_thread_name();

void
tctx_write_srcloc(char* file_name, U64 line_number);
void
tctx_read_srcloc(char** file_name, U64* line_number);
#define tctx_write_this_srcloc() tctx_write_srcloc(__FILE__, __LINE__)

#define ScratchBegin(conflicts, count) temp_begin(TCTX_ScratchGet((conflicts), (count)))
#define ScratchEnd(scratch) temp_end(scratch)

struct ScratchScope
{
    ScratchScope(Arena** conflicts, U64 count)
    {
        this->arena = TCTX_ScratchGet((conflicts), (count));
        this->pos = arena_pos(this->arena);
    }
    ~ScratchScope()
    {
        arena_pop_to(this->arena, this->pos);
    }

    Arena* arena;
    U64 pos;
};

//- rjf: lane metadata
LaneCtx
tctx_set_lane_ctx(LaneCtx lane_ctx);
void
tctx_lane_barrier_wait(void* broadcast_ptr, U64 broadcast_size, U64 broadcast_src_lane_idx);
#define lane_idx() (TCTX_Get()->lane_ctx.lane_idx)
#define lane_count() (TCTX_Get()->lane_ctx.lane_count)
#define lane_from_task_idx(idx) ((idx) % lane_count())
#define lane_ctx(ctx) tctx_set_lane_ctx((ctx))
#define lane_sync() tctx_lane_barrier_wait(0, 0, 0)
#define lane_sync_u64(ptr, src_lane_idx) tctx_lane_barrier_wait((ptr), sizeof(*(ptr)), (src_lane_idx))
#define lane_range(count) m_range_from_n_idx_m_count(lane_idx(), lane_count(), (count))

#endif // BASE_THREAD_CONTEXT_H
