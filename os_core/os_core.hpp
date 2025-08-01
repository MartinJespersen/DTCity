// Copyright (c) 2024 Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef OS_CORE_H
#define OS_CORE_H

////////////////////////////////
//~ rjf: System Info

typedef struct OS_SystemInfo OS_SystemInfo;
struct OS_SystemInfo
{
    U32 logical_processor_count;
    U64 page_size;
    U64 large_page_size;
    U64 allocation_granularity;
    String8 machine_name;
};

////////////////////////////////
//~ rjf: Process Info

typedef struct OS_ProcessInfo OS_ProcessInfo;
struct OS_ProcessInfo
{
    U32 pid;
    B32 large_pages_allowed;
    String8 binary_path;
    String8 initial_path;
    String8 user_program_data_path;
    String8List module_load_paths;
    String8List environment;
};

////////////////////////////////
//~ rjf: Access Flags

typedef U32 OS_AccessFlags;
enum
{
    OS_AccessFlag_Read = (1 << 0),
    OS_AccessFlag_Write = (1 << 1),
    OS_AccessFlag_Execute = (1 << 2),
    OS_AccessFlag_Append = (1 << 3),
    OS_AccessFlag_ShareRead = (1 << 4),
    OS_AccessFlag_ShareWrite = (1 << 5),
    OS_AccessFlag_Inherited = (1 << 6),
};

////////////////////////////////
//~ rjf: Files

typedef U32 OS_FileIterFlags;
enum
{
    OS_FileIterFlag_SkipFolders = (1 << 0),
    OS_FileIterFlag_SkipFiles = (1 << 1),
    OS_FileIterFlag_SkipHiddenFiles = (1 << 2),
    OS_FileIterFlag_Done = (1 << 31),
};

typedef struct OS_FileIter OS_FileIter;
struct OS_FileIter
{
    OS_FileIterFlags flags;
    U8 memory[800];
};

typedef struct OS_FileInfo OS_FileInfo;
struct OS_FileInfo
{
    String8 name;
    FileProperties props;
};

// nick: on-disk file identifier
typedef struct OS_FileID OS_FileID;
struct OS_FileID
{
    U64 v[3];
};

////////////////////////////////
//~ rjf: Handle Type

typedef struct OS_Handle OS_Handle;
struct OS_Handle
{
    U64 u64[1];
};

typedef struct OS_HandleNode OS_HandleNode;
struct OS_HandleNode
{
    OS_HandleNode* next;
    OS_Handle v;
};

typedef struct OS_HandleList OS_HandleList;
struct OS_HandleList
{
    OS_HandleNode* first;
    OS_HandleNode* last;
    U64 count;
};

typedef struct OS_HandleArray OS_HandleArray;
struct OS_HandleArray
{
    OS_Handle* v;
    U64 count;
};

////////////////////////////////
//~ rjf: Process Launch Parameters

typedef struct OS_ProcessLaunchParams OS_ProcessLaunchParams;
struct OS_ProcessLaunchParams
{
    String8List cmd_line;
    String8 path;
    String8List env;
    B32 inherit_env;
    B32 debug_subprocesses;
    B32 consoleless;
    OS_Handle stdout_file;
    OS_Handle stderr_file;
    OS_Handle stdin_file;
};

////////////////////////////////
//~ rjf: Thread Types

typedef void
OS_ThreadFunctionType(void* ptr);

////////////////////////////////
//~ rjf: Handle Type Functions (Helpers, Implemented Once)

static OS_Handle
OS_HandleIsZero(void);
static OS_Handle
OS_HandleFromPtr(void* ptr);
static B32
OS_HandleMatch(OS_Handle a, OS_Handle b);
static void
os_handle_list_push(Arena* arena, OS_HandleList* handles, OS_Handle handle);
static OS_HandleArray
os_handle_array_from_list(Arena* arena, OS_HandleList* list);

////////////////////////////////
//~ rjf: Command Line Argc/Argv Helper (Helper, Implemented Once)

static String8List
os_string_list_from_argcv(Arena* arena, int argc, char** argv);

////////////////////////////////
//~ rjf: Filesystem Helpers (Helpers, Implemented Once)

static String8
os_data_from_file_path(Arena* arena, String8 path);
static B32
os_write_data_to_file_path(String8 path, String8 data);
static B32
os_write_data_list_to_file_path(String8 path, String8List list);
static B32
os_append_data_to_file_path(String8 path, String8 data);
static OS_FileID
os_id_from_file_path(String8 path);
static S64
os_file_id_compare(OS_FileID a, OS_FileID b);
static String8
os_string_from_file_range(Arena* arena, OS_Handle file, Rng1U64 range);

////////////////////////////////
//~ rjf: Process Launcher Helpers

static OS_Handle
os_cmd_line_launch(String8 string);
static OS_Handle
os_cmd_line_launchf(char* fmt, ...);

////////////////////////////////
//~ rjf: @os_hooks System/Process Info (Implemented Per-OS)

static OS_SystemInfo*
OS_GetSystemInfo(void);
static OS_ProcessInfo*
os_get_process_info(void);
static String8
OS_GetCurrentPath(Arena* arena);
static U32
os_get_process_start_time_unix(void);
static inline String8
OS_PathDelimiter(void);

////////////////////////////////
//~ rjf: @os_hooks Memory Allocation (Implemented Per-OS)

//- rjf: basic
static void*
os_reserve(U64 size);
static B32
os_commit(void* ptr, U64 size);
static void
os_decommit(void* ptr, U64 size);
static void
os_release(void* ptr, U64 size);

// - rjf: large pages
static void*
os_reserve_large(U64 size);
static B32
os_commit_large(void* ptr, U64 size);

////////////////////////////////
//~ rjf: @os_hooks Thread Info (Implemented Per-OS)

static U32
os_tid(void);
static void
OS_SetThreadName(String8 string);

////////////////////////////////
//~ rjf: @os_hooks Aborting (Implemented Per-OS)

static void
os_abort(S32 exit_code);

////////////////////////////////
//~ rjf: @os_hooks File System (Implemented Per-OS)

//- rjf: files
static OS_Handle
os_file_open(OS_AccessFlags flags, String8 path);
static void
os_file_close(OS_Handle file);
static U64
os_file_read(OS_Handle file, Rng1U64 rng, void* out_data);
#define os_file_read_struct(f, off, ptr)                                                           \
    os_file_read((f), r1u64((off), (off) + sizeof(*(ptr))), (ptr))
static U64
os_file_write(OS_Handle file, Rng1U64 rng, void* data);
static B32
os_file_set_times(OS_Handle file, DateTime time);
static FileProperties
os_properties_from_file(OS_Handle file);
static OS_FileID
os_id_from_file(OS_Handle file);
static B32
os_file_reserve_size(OS_Handle file, U64 size);
static B32
os_delete_file_at_path(String8 path);
static B32
os_copy_file_path(String8 dst, String8 src);
static B32
os_move_file_path(String8 dst, String8 src);
static String8
os_full_path_from_path(Arena* arena, String8 path);
static B32
os_file_path_exists(String8 path);
static B32
os_folder_path_exists(String8 path);
static FileProperties
os_properties_from_file_path(String8 path);

//- rjf: file maps
static OS_Handle
os_file_map_open(OS_AccessFlags flags, OS_Handle file);
static void
os_file_map_close(OS_Handle map);
static void*
os_file_map_view_open(OS_Handle map, OS_AccessFlags flags, Rng1U64 range);
static void
os_file_map_view_close(OS_Handle map, void* ptr, Rng1U64 range);

//- rjf: directory iteration
static OS_FileIter*
os_file_iter_begin(Arena* arena, String8 path, OS_FileIterFlags flags);
static B32
os_file_iter_next(Arena* arena, OS_FileIter* iter, OS_FileInfo* info_out);
static void
os_file_iter_end(OS_FileIter* iter);

//- rjf: directory creation
static B32
os_make_directory(String8 path);

////////////////////////////////
//~ rjf: @os_hooks Shared Memory (Implemented Per-OS)

static OS_Handle
os_shared_memory_alloc(U64 size, String8 name);
static OS_Handle
os_shared_memory_open(String8 name);
static void
os_shared_memory_close(OS_Handle handle);
static void*
os_shared_memory_view_open(OS_Handle handle, Rng1U64 range);
static void
os_shared_memory_view_close(OS_Handle handle, void* ptr, Rng1U64 range);

////////////////////////////////
//~ rjf: @os_hooks Time (Implemented Per-OS)

static U64
os_now_microseconds(void);
static U32
os_now_unix(void);
static DateTime
os_now_universal_time(void);
static DateTime
os_universal_time_from_local(DateTime* local_time);
static DateTime
os_local_time_from_universal(DateTime* universal_time);
static void
os_sleep_milliseconds(U32 msec);

////////////////////////////////
//~ rjf: @os_hooks Child Processes (Implemented Per-OS)

static OS_Handle
os_process_launch(OS_ProcessLaunchParams* params);
static B32
os_process_join(OS_Handle handle, U64 endt_us);
static void
os_process_detach(OS_Handle handle);

////////////////////////////////
//~ rjf: @os_hooks Threads (Implemented Per-OS)

static OS_Handle
OS_ThreadLaunch(OS_ThreadFunctionType* func, void* ptr, void* params);
static B32
OS_ThreadJoin(OS_Handle handle, U64 endt_us);
static void
os_thread_detach(OS_Handle handle);

////////////////////////////////
//~ rjf: @os_hooks Synchronization Primitives (Implemented Per-OS)

//- rjf: recursive mutexes
static OS_Handle
OS_MutexAlloc(void);
static void
OS_MutexRelease(OS_Handle mutex);
static void
OS_MutexTake(OS_Handle mutex);
static void
OS_MutexDrop(OS_Handle mutex);

//- rjf: reader/writer mutexes
static OS_Handle
OS_RWMutexAlloc(void);
static void
OS_RWMutexRelease(OS_Handle rw_mutex);
static void
OS_RWMutexTakeR(OS_Handle mutex);
static void
OS_RWMutexDropR(OS_Handle mutex);
static void
OS_RWMutexTakeW(OS_Handle mutex);
static void
OS_RWMutexDropW(OS_Handle mutex);

//- rjf: condition variables
static OS_Handle
os_condition_variable_alloc(void);
static void
os_condition_variable_release(OS_Handle cv);
// returns false on timeout, true on signal, (max_wait_ms = max_U64) -> no timeout
static B32
os_condition_variable_wait(OS_Handle cv, OS_Handle mutex, U64 endt_us);
static B32
os_condition_variable_wait_rw_r(OS_Handle cv, OS_Handle mutex_rw, U64 endt_us);
static B32
os_condition_variable_wait_rw_w(OS_Handle cv, OS_Handle mutex_rw, U64 endt_us);
static void
os_condition_variable_signal(OS_Handle cv);
static void
os_condition_variable_broadcast(OS_Handle cv);

//- rjf: cross-process semaphores
static OS_Handle
OS_SemaphoreAlloc(U32 initial_count, U32 max_count, String8 name);
static void
OS_SemaphoreRelease(OS_Handle semaphore);
static OS_Handle
OS_SemaphoreOpen(String8 name);
static void
OS_SemaphoreClose(OS_Handle semaphore);
static B32
OS_SemaphoreTake(OS_Handle semaphore, U64 endt_us);
static void
OS_SemaphoreDrop(OS_Handle semaphore);

//- rjf: scope macros
#define OS_MutexScope(mutex) DeferLoop(OS_MutexTake(mutex), OS_MutexDrop(mutex))
#define OS_MutexScopeR(mutex) DeferLoop(OS_RWMutexTakeR(mutex), OS_RWMutexDropR(mutex))
#define OS_MutexScopeW(mutex) DeferLoop(OS_RWMutexTakeW(mutex), OS_RWMutexDropW(mutex))
#define OS_MutexScopeRWPromote(mutex)                                                              \
    DeferLoop((OS_RWMutexDropR(mutex), OS_RWMutexTakeW(mutex)),                                    \
              (OS_RWMutexDropW(mutex), OS_RWMutexTakeR(mutex)))

////////////////////////////////
//~ rjf: @os_hooks Dynamically-Loaded Libraries (Implemented Per-OS)

static OS_Handle
os_library_open(String8 path);
static void
os_library_close(OS_Handle lib);
static VoidProc*
os_library_load_proc(OS_Handle lib, String8 name);

////////////////////////////////
//~ rjf: @os_hooks Safe Calls (Implemented Per-OS)

static void
os_safe_call(OS_ThreadFunctionType* func, OS_ThreadFunctionType* fail_handler, void* ptr);

////////////////////////////////
//~ rjf: @os_hooks GUIDs (Implemented Per-OS)

static Guid
os_make_guid(void);

//~mgj: HotReload
// TODO: remove these forward declarations
struct DllInfo;
struct Context;

typedef void (*HotReloadFunc)(Context*);
//~ mgj: Entrypoint to the application from os layer
void App(HotReloadFunc);

static void
OS_GlobalStateSetFromPtr(void* ptr);

////////////////////////////////
//~ rjf: @os_hooks Entry Points (Implemented Per-OS)

// NOTE(rjf): The implementation of `os_core` will define low-level entry
// points if BUILD_ENTRY_DEFINING_UNIT is defined to 1. These will call
// into the standard codebase program entry points, named "entry_point".

// #if BUILD_ENTRY_DEFINING_UNIT
// raddbg_entry_point(entry_point);
// static void entry_point(CmdLine *cmdline);
// #endif

#endif // OS_CORE_H
