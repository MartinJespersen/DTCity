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

lib_internal Log*
LogAlloc();
lib_internal void
LogRelease(Log* log);

lib_internal void
LogMsg(LogMsgKind kind, String8 string);
lib_internal void
LogMsgF(LogMsgKind kind, char* fmt, ...);
#define LogInfo(s) LogMsg(LogMsgKind_Info, (s))
#define LogInfoF(...) LogMsgF(LogMsgKind_Info, __VA_ARGS__)
#define log_user_error(s) LogMsg(LogMsgKind_UserError, (s))
#define log_user_errorf(...) LogMsgF(LogMsgKind_UserError, __VA_ARGS__)

#define LogInfoNamedBlock(s) DeferLoop(LogInfoF("%s:\n{\n", ((s).str)), LogInfoF("}\n"))
#define LogInfoNamedBlockF(...)                                                                    \
    DeferLoop((LogInfoF(__VA_ARGS__), LogInfoF(":\n{\n")), LogInfoF("}\n"))

lib_internal void
LogScopeBegin();
lib_internal LogScopeResult
LogScopeEnd(Arena* arena);

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

    Log* log;
};

////////////////////////////////
// NOTE(allen): Thread Context Functions

lib_internal void
TCTX_InitAndEquip(TCTX* tctx);
lib_internal void
TCTX_Release();
lib_internal TCTX*
tctx_get_equipped();

lib_internal Arena*
TCTX_ScratchGet(Arena** conflicts, U64 countt);

lib_internal void
tctx_set_thread_name(String8 name);
lib_internal String8
tctx_get_thread_name();

lib_internal void
tctx_write_srcloc(char* file_name, U64 line_number);
lib_internal void
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

#endif // BASE_THREAD_CONTEXT_H
