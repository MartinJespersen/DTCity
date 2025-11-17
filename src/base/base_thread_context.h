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

static Log*
LogAlloc(void);
static void
LogRelease(Log* log);

static void
LogMsg(LogMsgKind kind, String8 string);
static void
LogMsgF(LogMsgKind kind, char* fmt, ...);
#define LogInfo(s) LogMsg(LogMsgKind_Info, (s))
#define LogInfoF(...) LogMsgF(LogMsgKind_Info, __VA_ARGS__)
#define log_user_error(s) LogMsg(LogMsgKind_UserError, (s))
#define log_user_errorf(...) LogMsgF(LogMsgKind_UserError, __VA_ARGS__)

#define LogInfoNamedBlock(s) DeferLoop(LogInfoF("%s:\n{\n", ((s).str)), LogInfoF("}\n"))
#define LogInfoNamedBlockF(...)                                                                    \
    DeferLoop((LogInfoF(__VA_ARGS__), LogInfoF(":\n{\n")), LogInfoF("}\n"))

static void
LogScopeBegin(void);
static LogScopeResult
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

static void
TCTX_InitAndEquip(TCTX* tctx);
static void
TCTX_Release(void);
static TCTX*
tctx_get_equipped(void);

static Arena*
TCTX_ScratchGet(Arena** conflicts, U64 countt);

static void
tctx_set_thread_name(String8 name);
static String8
tctx_get_thread_name(void);

static void
tctx_write_srcloc(char* file_name, U64 line_number);
static void
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
        ArenaPopTo(this->arena, this->pos);
    }

    Arena* arena;
    U64 pos;
};

#endif // BASE_THREAD_CONTEXT_H
