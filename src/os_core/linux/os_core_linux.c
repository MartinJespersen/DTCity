#include "diagnostics.hpp"
#include "base/base_inc.hpp"

lib_internal OS_LNX_State os_lnx_state = {0};

thread_static OS_LNX_SafeCallChain* os_lnx_safe_call_chain = 0;

// Copyright (c) 2024 Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Helpers

lib_internal DateTime
os_lnx_date_time_from_tm(tm in, U32 msec)
{
    DateTime dt = {0};
    dt.sec = in.tm_sec;
    dt.min = in.tm_min;
    dt.hour = in.tm_hour;
    dt.day = in.tm_mday - 1;
    dt.mon = in.tm_mon;
    dt.year = in.tm_year + 1900;
    dt.msec = msec;
    return dt;
}

lib_internal tm
os_lnx_tm_from_date_time(DateTime dt)
{
    tm result = {0};
    result.tm_sec = dt.sec;
    result.tm_min = dt.min;
    result.tm_hour = dt.hour;
    result.tm_mday = dt.day + 1;
    result.tm_mon = dt.mon;
    result.tm_year = dt.year - 1900;
    return result;
}

lib_internal timespec
os_lnx_timespec_from_date_time(DateTime dt)
{
    tm tm_val = os_lnx_tm_from_date_time(dt);
    time_t seconds = timegm(&tm_val);
    timespec result = {0};
    result.tv_sec = seconds;
    return result;
}

lib_internal DenseTime
os_lnx_dense_time_from_timespec(timespec in)
{
    DenseTime result = 0;
    {
        struct tm tm_time = {0};
        gmtime_r(&in.tv_sec, &tm_time);
        DateTime date_time = os_lnx_date_time_from_tm(tm_time, in.tv_nsec / Million(1));
        result = dense_time_from_date_time(date_time);
    }
    return result;
}

lib_internal FileProperties
os_lnx_file_properties_from_stat(struct stat* s)
{
    FileProperties props = {0};
    props.size = s->st_size;
    props.created = os_lnx_dense_time_from_timespec(s->st_ctim);
    props.modified = os_lnx_dense_time_from_timespec(s->st_mtim);
    if (S_ISDIR(s->st_mode))
    {
        props.flags |= FilePropertyFlag_IsFolder;
    }
    return props;
}

////////////////////////////////
//~ rjf: Entities

lib_internal OS_LNX_Entity*
os_lnx_entity_alloc(OS_LNX_EntityKind kind)
{
    OS_LNX_Entity* entity = 0;
    DeferLoop(pthread_mutex_lock(&os_lnx_state.entity_mutex), pthread_mutex_unlock(&os_lnx_state.entity_mutex))
    {
        entity = os_lnx_state.entity_free;
        if (entity)
        {
            SLLStackPop(os_lnx_state.entity_free);
        }
        else
        {
            entity = PushArrayNoZero(os_lnx_state.entity_arena, OS_LNX_Entity, 1);
        }
    }
    MemoryZeroStruct(entity);
    entity->kind = kind;
    return entity;
}

lib_internal void
os_lnx_entity_release(OS_LNX_Entity* entity)
{
    DeferLoop(pthread_mutex_lock(&os_lnx_state.entity_mutex), pthread_mutex_unlock(&os_lnx_state.entity_mutex))
    {
        SLLStackPush(os_lnx_state.entity_free, entity);
    }
}

////////////////////////////////
//~ rjf: Thread Entry Point

lib_internal void*
os_lnx_thread_entry_point(void* ptr)
{
    _os_lnx_crash_thread_init();
    OS_LNX_Entity* entity = (OS_LNX_Entity*)ptr;
    OS_ThreadFunctionType* func = entity->thread.func;
    void* thread_ptr = entity->thread.ptr;
    TCTX tctx_;
    TCTX_InitAndEquip(&tctx_);
    func(thread_ptr);
    TCTX_Release();
    return 0;
}

////////////////////////////////
//~ rjf: @os_hooks System/Process Info (Implemented Per-OS)

OS_SystemInfo*
OS_GetSystemInfo()
{
    return &os_lnx_state.system_info;
}

OS_ProcessInfo*
os_get_process_info()
{
    return &os_lnx_state.process_info;
}

String8
os_current_path_get(Arena* arena)
{
    char* cwdir = getcwd(0, 0);
    String8 string = push_str8_copy(arena, str8_c_string(cwdir));
    free(cwdir);
    return string;
}

String8
os_path_delimiter()
{
    return str8_lit("/");
}

U32
os_get_process_start_time_unix()
{
    Temp scratch = ScratchBegin(0, 0);
    U64 start_time = 0;
    pid_t pid = getpid();
    String8 path = push_str8f(scratch.arena, "/proc/%u", pid);
    struct stat st;
    int err = stat((char*)path.str, &st);
    if (err == 0)
    {
        start_time = st.st_mtime;
    }
    ScratchEnd(scratch);
    return (U32)start_time;
}

////////////////////////////////
//~ rjf: @os_hooks Memory Allocation (Implemented Per-OS)

//- rjf: basic

void*
os_reserve(U64 size)
{
    void* result = mmap(0, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (result == MAP_FAILED)
    {
        result = 0;
    }
    return result;
}

B32
os_commit(void* ptr, U64 size)
{
    mprotect(ptr, size, PROT_READ | PROT_WRITE);
    return 1;
}

void
os_decommit(void* ptr, U64 size)
{
    madvise(ptr, size, MADV_DONTNEED);
    mprotect(ptr, size, PROT_NONE);
}

void
os_release(void* ptr, U64 size)
{
    munmap(ptr, size);
}

//- rjf: large pages

void*
os_reserve_large(U64 size)
{
    void* result = mmap(0, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    if (result == MAP_FAILED)
    {
        result = 0;
    }
    return result;
}

B32
os_commit_large(void* ptr, U64 size)
{
    mprotect(ptr, size, PROT_READ | PROT_WRITE);
    return 1;
}

////////////////////////////////
//~ rjf: @os_hooks Thread Info (Implemented Per-OS)

U32
os_tid()
{
    U32 result = gettid();
    return result;
}

void
os_set_thread_name(String8 name)
{
    Temp scratch = ScratchBegin(0, 0);
    String8 name_copy = push_str8_copy(scratch.arena, name);
    pthread_t current_thread = pthread_self();
    pthread_setname_np(current_thread, (char*)name_copy.str);
    ScratchEnd(scratch);
}

////////////////////////////////
//~ rjf: @os_hooks Aborting (Implemented Per-OS)

void
os_abort(S32 exit_code)
{
    exit(exit_code);
}

////////////////////////////////
//~ rjf: @os_hooks File System (Implemented Per-OS)

//- rjf: files

OS_Handle
os_file_open(OS_AccessFlags flags, String8 path)
{
    Temp scratch = ScratchBegin(0, 0);
    String8 path_copy = push_str8_copy(scratch.arena, path);
    int lnx_flags = 0;
    if (flags & OS_AccessFlag_Read && flags & OS_AccessFlag_Write)
    {
        lnx_flags = O_RDWR;
    }
    else if (flags & OS_AccessFlag_Write)
    {
        lnx_flags = O_WRONLY;
    }
    else if (flags & OS_AccessFlag_Read)
    {
        lnx_flags = O_RDONLY;
    }
    if (flags & OS_AccessFlag_Append)
    {
        lnx_flags |= O_APPEND;
    }
    if (flags & (OS_AccessFlag_Write | OS_AccessFlag_Append))
    {
        lnx_flags |= O_CREAT;
    }
    int fd = open((char*)path_copy.str, lnx_flags, 0755);
    OS_Handle handle = {0};
    if (fd != -1)
    {
        handle.u64[0] = fd;
    }
    ScratchEnd(scratch);
    return handle;
}

void
os_file_close(OS_Handle file)
{
    if (OS_HandleMatch(file, OS_HandleIsZero()))
    {
        return;
    }
    int fd = (int)file.u64[0];
    close(fd);
}

U64
os_file_read(OS_Handle file, Rng1U64 rng, void* out_data)
{
    if (OS_HandleMatch(file, OS_HandleIsZero()))
    {
        return 0;
    }
    int fd = (int)file.u64[0];
    U64 total_num_bytes_to_read = dim_1u64(rng);
    U64 total_num_bytes_read = 0;
    U64 total_num_bytes_left_to_read = total_num_bytes_to_read;
    for (; total_num_bytes_left_to_read > 0;)
    {
        int read_result = pread(fd, (U8*)out_data + total_num_bytes_read, total_num_bytes_left_to_read,
                                rng.min + total_num_bytes_read);
        if (read_result >= 0)
        {
            total_num_bytes_read += read_result;
            total_num_bytes_left_to_read -= read_result;
        }
        else if (errno != EINTR)
        {
            break;
        }
    }
    return total_num_bytes_read;
}

U64
os_file_write(OS_Handle file, Rng1U64 rng, void* data)
{
    if (OS_HandleMatch(file, OS_HandleIsZero()))
    {
        return 0;
    }
    int fd = (int)file.u64[0];
    U64 total_num_bytes_to_write = dim_1u64(rng);
    U64 total_num_bytes_written = 0;
    U64 total_num_bytes_left_to_write = total_num_bytes_to_write;
    for (; total_num_bytes_left_to_write > 0;)
    {
        int write_result = pwrite(fd, (U8*)data + total_num_bytes_written, total_num_bytes_left_to_write,
                                  rng.min + total_num_bytes_written);
        if (write_result >= 0)
        {
            total_num_bytes_written += write_result;
            total_num_bytes_left_to_write -= write_result;
        }
        else if (errno != EINTR)
        {
            break;
        }
    }
    return total_num_bytes_written;
}

B32
os_file_set_times(OS_Handle file, DateTime date_time)
{
    if (OS_HandleMatch(file, OS_HandleIsZero()))
    {
        return 0;
    }
    int fd = (int)file.u64[0];
    timespec time = os_lnx_timespec_from_date_time(date_time);
    timespec times[2] = {time, time};
    int futimens_result = futimens(fd, times);
    B32 good = (futimens_result != -1);
    return good;
}

FileProperties
os_properties_from_file(OS_Handle file)
{
    if (OS_HandleMatch(file, OS_HandleIsZero()))
    {
        return (FileProperties){0};
    }
    int fd = (int)file.u64[0];
    struct stat fd_stat = {0};
    int fstat_result = fstat(fd, &fd_stat);
    FileProperties props = {0};
    if (fstat_result != -1)
    {
        props = os_lnx_file_properties_from_stat(&fd_stat);
    }
    return props;
}

OS_FileID
os_id_from_file(OS_Handle file)
{
    if (OS_HandleMatch(file, OS_HandleIsZero()))
    {
        return (OS_FileID){0};
    }
    int fd = (int)file.u64[0];
    struct stat fd_stat = {0};
    int fstat_result = fstat(fd, &fd_stat);
    OS_FileID id = {0};
    if (fstat_result != -1)
    {
        id.v[0] = fd_stat.st_dev;
        id.v[1] = fd_stat.st_ino;
    }
    return id;
}

B32
os_delete_file_at_path(String8 path)
{
    Temp scratch = ScratchBegin(0, 0);
    B32 result = 0;
    String8 path_copy = push_str8_copy(scratch.arena, path);
    if (remove((char*)path_copy.str) != -1)
    {
        result = 1;
    }
    ScratchEnd(scratch);
    return result;
}

B32
os_delete_directory_at_path(String8 path)
{
    Temp scratch = ScratchBegin(0, 0);
    B32 result = 0;
    String8 path_copy = push_str8_copy(scratch.arena, path);
    if (rmdir((char*)path_copy.str) != -1)
    {
        result = 1;
    }
    ScratchEnd(scratch);
    return result;
}

B32
os_copy_file_path(String8 dst, String8 src)
{
    B32 result = 0;
    OS_Handle src_h = os_file_open(OS_AccessFlag_Read, src);
    OS_Handle dst_h = os_file_open(OS_AccessFlag_Write, dst);
    if (!OS_HandleMatch(src_h, OS_HandleIsZero()) && !OS_HandleMatch(dst_h, OS_HandleIsZero()))
    {
        int src_fd = (int)src_h.u64[0];
        int dst_fd = (int)dst_h.u64[0];
        FileProperties src_props = os_properties_from_file(src_h);
        U64 size = src_props.size;
        U64 total_bytes_copied = 0;
        U64 bytes_left_to_copy = size;
        for (; bytes_left_to_copy > 0;)
        {
            off_t sendfile_off = total_bytes_copied;
            int send_result = sendfile(dst_fd, src_fd, &sendfile_off, bytes_left_to_copy);
            if (send_result <= 0)
            {
                break;
            }
            U64 bytes_copied = (U64)send_result;
            bytes_left_to_copy -= bytes_copied;
            total_bytes_copied += bytes_copied;
        }
    }
    os_file_close(src_h);
    os_file_close(dst_h);
    return result;
}

B32
os_move_file_path(String8 dst, String8 src)
{
    // TODO(rjf)
    return false;
}

String8
os_full_path_from_path(Arena* arena, String8 path)
{
    Temp scratch = ScratchBegin(&arena, 1);
    String8 path_copy = push_str8_copy(scratch.arena, path);
    char buffer[PATH_MAX] = {0};
    realpath((char*)path_copy.str, buffer);
    String8 result = push_str8_copy(arena, str8_c_string(buffer));
    ScratchEnd(scratch);
    return result;
}

B32
os_file_path_exists(String8 path)
{
    Temp scratch = ScratchBegin(0, 0);
    String8 path_copy = push_str8_copy(scratch.arena, path);
    int access_result = access((char*)path_copy.str, F_OK);
    B32 result = 0;
    if (access_result == 0)
    {
        result = 1;
    }
    ScratchEnd(scratch);
    return result;
}

B32
os_folder_path_exists(String8 path)
{
    Temp scratch = ScratchBegin(0, 0);
    B32 exists = 0;
    String8 path_copy = push_str8_copy(scratch.arena, path);
    DIR* handle = opendir((char*)path_copy.str);
    if (handle)
    {
        closedir(handle);
        exists = 1;
    }
    ScratchEnd(scratch);
    return exists;
}

FileProperties
os_properties_from_file_path(String8 path)
{
    Temp scratch = ScratchBegin(0, 0);
    String8 path_copy = push_str8_copy(scratch.arena, path);
    struct stat f_stat = {0};
    int stat_result = stat((char*)path_copy.str, &f_stat);
    FileProperties props = {0};
    if (stat_result != -1)
    {
        props = os_lnx_file_properties_from_stat(&f_stat);
    }
    ScratchEnd(scratch);
    return props;
}

//- rjf: file maps

OS_Handle
os_file_map_open(OS_AccessFlags flags, OS_Handle file)
{
    OS_Handle map = file;
    return map;
}

void
os_file_map_close(OS_Handle map)
{
    // NOTE(rjf): nothing to do; `map` handles are the same as `file` handles
    // in
    // the linux implementation (on Windows they require separate handles)
}

void*
os_file_map_view_open(OS_Handle map, OS_AccessFlags flags, Rng1U64 range)
{
    if (OS_HandleMatch(map, OS_HandleIsZero()))
    {
        return 0;
    }
    int fd = (int)map.u64[0];
    int prot_flags = 0;
    if (flags & OS_AccessFlag_Write)
    {
        prot_flags |= PROT_WRITE;
    }
    if (flags & OS_AccessFlag_Read)
    {
        prot_flags |= PROT_READ;
    }
    int map_flags = MAP_PRIVATE;
    void* base = mmap(0, dim_1u64(range), prot_flags, map_flags, fd, range.min);
    if (base == MAP_FAILED)
    {
        base = 0;
    }
    return base;
}

void
os_file_map_view_close(OS_Handle map, void* ptr, Rng1U64 range)
{
    munmap(ptr, dim_1u64(range));
}

//- rjf: directory iteration

OS_FileIter*
os_file_iter_begin(Arena* arena, String8 path, OS_FileIterFlags flags)
{
    OS_FileIter* base_iter = PushArray(arena, OS_FileIter, 1);
    base_iter->flags = flags;
    OS_LNX_FileIter* iter = (OS_LNX_FileIter*)base_iter->memory;
    {
        String8 path_copy = push_str8_copy(arena, path);
        iter->dir = opendir((char*)path_copy.str);
        iter->path = path_copy;
    }
    return base_iter;
}

B32
os_file_iter_next(Arena* arena, OS_FileIter* iter, OS_FileInfo* info_out)
{
    B32 good = 0;
    OS_LNX_FileIter* lnx_iter = (OS_LNX_FileIter*)iter->memory;
    for (;;)
    {
        // rjf: get next entry
        lnx_iter->dp = readdir(lnx_iter->dir);
        good = (lnx_iter->dp != 0);

        // rjf: unpack entry info
        struct stat st = {0};
        int stat_result = 0;
        if (good)
        {
            Temp scratch = ScratchBegin(&arena, 1);
            String8 full_path = push_str8f(scratch.arena, "%s/%s", lnx_iter->path.str, lnx_iter->dp->d_name);
            stat_result = lstat((char*)full_path.str, &st);
            ScratchEnd(scratch);
        }

        // rjf: determine if filtered
        B32 filtered = 0;
        if (good)
        {
            filtered =
                ((S_ISDIR(st.st_mode) && iter->flags & OS_FileIterFlag_SkipFolders) ||
                 (S_ISREG(st.st_mode) && iter->flags & OS_FileIterFlag_SkipFiles) ||
                 (lnx_iter->dp->d_name[0] == '.' && lnx_iter->dp->d_name[1] == 0) ||
                 (lnx_iter->dp->d_name[0] == '.' && lnx_iter->dp->d_name[1] == '.' && lnx_iter->dp->d_name[2] == 0));
        }

        // rjf: output & exit, if good & unfiltered
        if (good && !filtered)
        {
            info_out->name = push_str8_copy(arena, str8_c_string(lnx_iter->dp->d_name));
            if (stat_result != -1)
            {
                info_out->props = os_lnx_file_properties_from_stat(&st);
                if (S_ISLNK(st.st_mode))
                {
                    info_out->props.flags |= FilePropertyFlag_IsLink;
                }
            }
            break;
        }

        // rjf: exit if not good
        if (!good)
        {
            break;
        }
    }
    return good;
}

void
os_file_iter_end(OS_FileIter* iter)
{
    OS_LNX_FileIter* lnx_iter = (OS_LNX_FileIter*)iter->memory;
    closedir(lnx_iter->dir);
}

//- rjf: directory creation

B32
os_make_directory(String8 path)
{
    Temp scratch = ScratchBegin(0, 0);
    B32 result = 0;
    String8 path_copy = push_str8_copy(scratch.arena, path);
    if (mkdir((char*)path_copy.str, 0755) != -1)
    {
        result = 1;
    }
    ScratchEnd(scratch);
    return result;
}

////////////////////////////////
//~ rjf: @os_hooks Shared Memory (Implemented Per-OS)

OS_Handle
os_shared_memory_alloc(U64 size, String8 name)
{
    Temp scratch = ScratchBegin(0, 0);
    String8 name_copy = push_str8_copy(scratch.arena, name);
    int id = shm_open((char*)name_copy.str, O_RDWR, 0);
    ftruncate(id, size);
    OS_Handle result = {(U64)id};
    ScratchEnd(scratch);
    return result;
}

OS_Handle
os_shared_memory_open(String8 name)
{
    Temp scratch = ScratchBegin(0, 0);
    String8 name_copy = push_str8_copy(scratch.arena, name);
    int id = shm_open((char*)name_copy.str, O_RDWR, 0);
    OS_Handle result = {(U64)id};
    ScratchEnd(scratch);
    return result;
}

void
os_shared_memory_close(OS_Handle handle)
{
    if (OS_HandleMatch(handle, OS_HandleIsZero()))
    {
        return;
    }
    int id = (int)handle.u64[0];
    close(id);
}

void*
os_shared_memory_view_open(OS_Handle handle, Rng1U64 range)
{
    if (OS_HandleMatch(handle, OS_HandleIsZero()))
    {
        return 0;
    }
    int id = (int)handle.u64[0];
    void* base = mmap(0, dim_1u64(range), PROT_READ | PROT_WRITE, MAP_SHARED, id, range.min);
    if (base == MAP_FAILED)
    {
        base = 0;
    }
    return base;
}

void
os_shared_memory_view_close(OS_Handle handle, void* ptr, Rng1U64 range)
{
    if (OS_HandleMatch(handle, OS_HandleIsZero()))
    {
        return;
    }
    munmap(ptr, dim_1u64(range));
}

////////////////////////////////
//~ rjf: @os_hooks Time (Implemented Per-OS)

U64
os_now_microseconds()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    U64 result = t.tv_sec * Million(1) + (t.tv_nsec / Thousand(1));
    return result;
}

U32
os_now_unix()
{
    time_t t = time(0);
    return (U32)t;
}

DateTime
os_now_universal_time()
{
    time_t t = 0;
    time(&t);
    struct tm universal_tm = {0};
    gmtime_r(&t, &universal_tm);
    DateTime result = os_lnx_date_time_from_tm(universal_tm, 0);
    return result;
}

DateTime
os_universal_time_from_local(DateTime* date_time)
{
    // rjf: local DateTime -> universal time_t
    tm local_tm = os_lnx_tm_from_date_time(*date_time);
    local_tm.tm_isdst = -1;
    time_t universal_t = mktime(&local_tm);

    // rjf: universal time_t -> DateTime
    tm universal_tm = {0};
    gmtime_r(&universal_t, &universal_tm);
    DateTime result = os_lnx_date_time_from_tm(universal_tm, 0);
    return result;
}

DateTime
os_local_time_from_universal(DateTime* date_time)
{
    // rjf: universal DateTime -> local time_t
    tm universal_tm = os_lnx_tm_from_date_time(*date_time);
    universal_tm.tm_isdst = -1;
    time_t universal_t = timegm(&universal_tm);
    tm local_tm = {0};
    localtime_r(&universal_t, &local_tm);

    // rjf: local tm -> DateTime
    DateTime result = os_lnx_date_time_from_tm(local_tm, 0);
    return result;
}

void
os_sleep_milliseconds(U32 msec)
{
    usleep(msec * Thousand(1));
}

////////////////////////////////
//~ rjf: @os_hooks Child Processes (Implemented Per-OS)

OS_Handle
os_process_launch(OS_ProcessLaunchParams* params)
{
    NotImplemented;
}

B32
os_process_join(OS_Handle handle, U64 endt_us)
{
    NotImplemented;
}

void
os_process_detach(OS_Handle handle)
{
    NotImplemented;
}

////////////////////////////////
//~ rjf: @os_hooks Threads (Implemented Per-OS)

OS_Handle
OS_ThreadLaunch(OS_ThreadFunctionType* func, void* ptr, void* params)
{
    OS_LNX_Entity* entity = os_lnx_entity_alloc(OS_LNX_EntityKind_Thread);
    entity->thread.func = func;
    entity->thread.ptr = ptr;
    {
        int pthread_result = pthread_create(&entity->thread.handle, 0, os_lnx_thread_entry_point, entity);
        if (pthread_result == -1)
        {
            os_lnx_entity_release(entity);
            entity = 0;
        }
    }
    OS_Handle handle = {(U64)entity};
    return handle;
}

B32
OS_ThreadJoin(OS_Handle handle, U64 endt_us)
{
    if (OS_HandleMatch(handle, OS_HandleIsZero()))
    {
        return 0;
    }
    OS_LNX_Entity* entity = (OS_LNX_Entity*)handle.u64[0];
    int join_result = pthread_join(entity->thread.handle, 0);
    B32 result = (join_result == 0);
    os_lnx_entity_release(entity);
    return result;
}

void
os_thread_detach(OS_Handle handle)
{
    if (OS_HandleMatch(handle, OS_HandleIsZero()))
    {
        return;
    }
    OS_LNX_Entity* entity = (OS_LNX_Entity*)handle.u64[0];
    os_lnx_entity_release(entity);
}

////////////////////////////////
//~ rjf: @os_hooks Synchronization Primitives (Implemented Per-OS)

//- rjf: mutexes

OS_Handle
OS_MutexAlloc()
{
    OS_LNX_Entity* entity = os_lnx_entity_alloc(OS_LNX_EntityKind_Mutex);
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    int init_result = pthread_mutex_init(&entity->mutex_handle, &attr);
    pthread_mutexattr_destroy(&attr);
    if (init_result == -1)
    {
        os_lnx_entity_release(entity);
        entity = 0;
    }
    OS_Handle handle = {(U64)entity};
    return handle;
}
void
OS_MutexRelease(OS_Handle mutex)
{
    if (OS_HandleMatch(mutex, OS_HandleIsZero()))
    {
        return;
    }
    OS_LNX_Entity* entity = (OS_LNX_Entity*)mutex.u64[0];
    pthread_mutex_destroy(&entity->mutex_handle);
    os_lnx_entity_release(entity);
}

void
os_mutex_take(OS_Handle mutex)
{
    if (OS_HandleMatch(mutex, OS_HandleIsZero()))
    {
        return;
    }
    OS_LNX_Entity* entity = (OS_LNX_Entity*)mutex.u64[0];
    pthread_mutex_lock(&entity->mutex_handle);
}

void
os_mutex_drop(OS_Handle mutex)
{
    if (OS_HandleMatch(mutex, OS_HandleIsZero()))
    {
        return;
    }
    OS_LNX_Entity* entity = (OS_LNX_Entity*)mutex.u64[0];
    pthread_mutex_unlock(&entity->mutex_handle);
}

//- rjf: reader/writer mutexes

OS_Handle
os_rw_mutex_alloc()
{
    OS_LNX_Entity* entity = os_lnx_entity_alloc(OS_LNX_EntityKind_RWMutex);
    int init_result = pthread_rwlock_init(&entity->rwmutex_handle, 0);
    if (init_result == -1)
    {
        os_lnx_entity_release(entity);
        entity = 0;
    }
    OS_Handle handle = {(U64)entity};
    return handle;
}

void
os_rw_mutex_release(OS_Handle rw_mutex)
{
    if (OS_HandleMatch(rw_mutex, OS_HandleIsZero()))
    {
        return;
    }
    OS_LNX_Entity* entity = (OS_LNX_Entity*)rw_mutex.u64[0];
    pthread_rwlock_destroy(&entity->rwmutex_handle);
    os_lnx_entity_release(entity);
}

void
os_rw_mutex_take_r(OS_Handle rw_mutex)
{
    if (OS_HandleMatch(rw_mutex, OS_HandleIsZero()))
    {
        return;
    }
    OS_LNX_Entity* entity = (OS_LNX_Entity*)rw_mutex.u64[0];
    pthread_rwlock_rdlock(&entity->rwmutex_handle);
}

void
os_rw_mutex_drop_r(OS_Handle rw_mutex)
{
    if (OS_HandleMatch(rw_mutex, OS_HandleIsZero()))
    {
        return;
    }
    OS_LNX_Entity* entity = (OS_LNX_Entity*)rw_mutex.u64[0];
    pthread_rwlock_unlock(&entity->rwmutex_handle);
}

void
os_rw_mutex_take_w(OS_Handle rw_mutex)
{
    if (OS_HandleMatch(rw_mutex, OS_HandleIsZero()))
    {
        return;
    }
    OS_LNX_Entity* entity = (OS_LNX_Entity*)rw_mutex.u64[0];
    pthread_rwlock_wrlock(&entity->rwmutex_handle);
}

void
os_rw_mutex_drop_w(OS_Handle rw_mutex)
{
    if (OS_HandleMatch(rw_mutex, OS_HandleIsZero()))
    {
        return;
    }
    OS_LNX_Entity* entity = (OS_LNX_Entity*)rw_mutex.u64[0];
    pthread_rwlock_unlock(&entity->rwmutex_handle);
}

//- rjf: condition variables

OS_Handle
os_condition_variable_alloc()
{
    OS_LNX_Entity* entity = os_lnx_entity_alloc(OS_LNX_EntityKind_ConditionVariable);
    int init_result = pthread_cond_init(&entity->cv.cond_handle, 0);
    if (init_result == -1)
    {
        os_lnx_entity_release(entity);
        entity = 0;
    }
    int init2_result = 0;
    if (entity)
    {
        init2_result = pthread_mutex_init(&entity->cv.rwlock_mutex_handle, 0);
    }
    if (init2_result == -1)
    {
        pthread_cond_destroy(&entity->cv.cond_handle);
        os_lnx_entity_release(entity);
        entity = 0;
    }
    OS_Handle handle = {(U64)entity};
    return handle;
}

void
os_condition_variable_release(OS_Handle cv)
{
    if (OS_HandleMatch(cv, OS_HandleIsZero()))
    {
        return;
    }
    OS_LNX_Entity* entity = (OS_LNX_Entity*)cv.u64[0];
    pthread_cond_destroy(&entity->cv.cond_handle);
    pthread_mutex_destroy(&entity->cv.rwlock_mutex_handle);
    os_lnx_entity_release(entity);
}

B32
os_condition_variable_wait(OS_Handle cv, OS_Handle mutex, U64 endt_us)
{
    if (OS_HandleMatch(cv, OS_HandleIsZero()))
    {
        return 0;
    }
    if (OS_HandleMatch(mutex, OS_HandleIsZero()))
    {
        return 0;
    }
    OS_LNX_Entity* cv_entity = (OS_LNX_Entity*)cv.u64[0];
    OS_LNX_Entity* mutex_entity = (OS_LNX_Entity*)mutex.u64[0];
    struct timespec endt_timespec;
    endt_timespec.tv_sec = endt_us / Million(1);
    endt_timespec.tv_nsec = Thousand(1) * (endt_us - (endt_us / Million(1)) * Million(1));
    int wait_result = pthread_cond_timedwait(&cv_entity->cv.cond_handle, &mutex_entity->mutex_handle, &endt_timespec);
    B32 result = (wait_result != ETIMEDOUT);
    return result;
}

B32
os_condition_variable_wait_rw_r(OS_Handle cv, OS_Handle mutex_rw, U64 endt_us)
{
    // TODO(rjf): because pthread does not supply cv/rw natively, I had to hack
    // this together, but this would probably just be a lot better if we just
    // implemented the primitives ourselves with e.g. futexes
    //
    if (OS_HandleMatch(cv, OS_HandleIsZero()))
    {
        return 0;
    }
    if (OS_HandleMatch(mutex_rw, OS_HandleIsZero()))
    {
        return 0;
    }
    OS_LNX_Entity* cv_entity = (OS_LNX_Entity*)cv.u64[0];
    OS_LNX_Entity* rw_mutex_entity = (OS_LNX_Entity*)mutex_rw.u64[0];
    struct timespec endt_timespec;
    endt_timespec.tv_sec = endt_us / Million(1);
    endt_timespec.tv_nsec = Thousand(1) * (endt_us - (endt_us / Million(1)) * Million(1));
    B32 result = 0;
    for (;;)
    {
        pthread_mutex_lock(&cv_entity->cv.rwlock_mutex_handle);
        int wait_result =
            pthread_cond_timedwait(&cv_entity->cv.cond_handle, &cv_entity->cv.rwlock_mutex_handle, &endt_timespec);
        if (wait_result != ETIMEDOUT)
        {
            pthread_rwlock_rdlock(&rw_mutex_entity->rwmutex_handle);
            pthread_mutex_unlock(&cv_entity->cv.rwlock_mutex_handle);
            result = 1;
            break;
        }
        pthread_mutex_unlock(&cv_entity->cv.rwlock_mutex_handle);
        if (wait_result == ETIMEDOUT)
        {
            break;
        }
    }
    return result;
}

B32
os_condition_variable_wait_rw_w(OS_Handle cv, OS_Handle mutex_rw, U64 endt_us)
{
    // TODO(rjf): because pthread does not supply cv/rw natively, I had to hack
    // this together, but this would probably just be a lot better if we just
    // implemented the primitives ourselves with e.g. futexes
    //
    if (OS_HandleMatch(cv, OS_HandleIsZero()))
    {
        return 0;
    }
    if (OS_HandleMatch(mutex_rw, OS_HandleIsZero()))
    {
        return 0;
    }
    OS_LNX_Entity* cv_entity = (OS_LNX_Entity*)cv.u64[0];
    OS_LNX_Entity* rw_mutex_entity = (OS_LNX_Entity*)mutex_rw.u64[0];
    struct timespec endt_timespec;
    endt_timespec.tv_sec = endt_us / Million(1);
    endt_timespec.tv_nsec = Thousand(1) * (endt_us - (endt_us / Million(1)) * Million(1));
    B32 result = 0;
    for (;;)
    {
        pthread_mutex_lock(&cv_entity->cv.rwlock_mutex_handle);
        int wait_result =
            pthread_cond_timedwait(&cv_entity->cv.cond_handle, &cv_entity->cv.rwlock_mutex_handle, &endt_timespec);
        if (wait_result != ETIMEDOUT)
        {
            pthread_rwlock_wrlock(&rw_mutex_entity->rwmutex_handle);
            pthread_mutex_unlock(&cv_entity->cv.rwlock_mutex_handle);
            result = 1;
            break;
        }
        pthread_mutex_unlock(&cv_entity->cv.rwlock_mutex_handle);
        if (wait_result == ETIMEDOUT)
        {
            break;
        }
    }
    return result;
}

void
os_condition_variable_signal(OS_Handle cv)
{
    if (OS_HandleMatch(cv, OS_HandleIsZero()))
    {
        return;
    }
    OS_LNX_Entity* cv_entity = (OS_LNX_Entity*)cv.u64[0];
    pthread_cond_signal(&cv_entity->cv.cond_handle);
}

void
os_condition_variable_broadcast(OS_Handle cv)
{
    if (OS_HandleMatch(cv, OS_HandleIsZero()))
    {
        return;
    }
    OS_LNX_Entity* cv_entity = (OS_LNX_Entity*)cv.u64[0];
    pthread_cond_broadcast(&cv_entity->cv.cond_handle);
}

RWMutex
rw_mutex_alloc()
{
    OS_Handle handle = os_rw_mutex_alloc();
    RWMutex mutex = {handle.u64[0]};
    return mutex;
}

void
rw_mutex_release(RWMutex mutex)
{
    OS_Handle handle = {mutex.u64[0]};
    os_rw_mutex_release(handle);
}

void
rw_mutex_take(RWMutex mutex, B32 write_mode)
{
    OS_Handle handle = {mutex.u64[0]};
    if (write_mode)
    {
        os_rw_mutex_take_w(handle);
    }
    else
    {
        os_rw_mutex_take_r(handle);
    }
}

void
rw_mutex_drop(RWMutex mutex, B32 write_mode)
{
    OS_Handle handle = {mutex.u64[0]};
    if (write_mode)
    {
        os_rw_mutex_drop_w(handle);
    }
    else
    {
        os_rw_mutex_drop_r(handle);
    }
}

CondVar
cond_var_alloc()
{
    OS_Handle handle = os_condition_variable_alloc();
    CondVar cv = {handle.u64[0]};
    return cv;
}

void
cond_var_release(CondVar cv)
{
    OS_Handle handle = {cv.u64[0]};
    os_condition_variable_release(handle);
}

B32
cond_var_wait_rw(CondVar cv, RWMutex mutex, B32 write_mode, U64 endt_us)
{
    OS_Handle cv_handle = {cv.u64[0]};
    OS_Handle mutex_handle = {mutex.u64[0]};
    B32 result = 0;
    if (write_mode)
    {
        result = os_condition_variable_wait_rw_w(cv_handle, mutex_handle, endt_us);
    }
    else
    {
        result = os_condition_variable_wait_rw_r(cv_handle, mutex_handle, endt_us);
    }
    return result;
}

void
cond_var_signal(CondVar cv)
{
    OS_Handle handle = {cv.u64[0]};
    os_condition_variable_signal(handle);
}

//- rjf: cross-process semaphores

OS_Handle
OS_SemaphoreAlloc(U32 initial_count, U32 max_count, String8 name)
{
    OS_Handle result = {0};
    if (name.size > 0)
    {
        // TODO: we need to allocate shared memory to store sem_t
        NotImplemented;
    }
    else
    {
        sem_t* s = (sem_t*)mmap(0, sizeof(*s), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        AssertAlways(s != MAP_FAILED);
        int err = sem_init(s, 0, initial_count);
        if (err == 0)
        {
            result.u64[0] = (U64)s;
        }
    }
    return result;
}

void
OS_SemaphoreRelease(OS_Handle semaphore)
{
    int err = munmap((void*)semaphore.u64[0], sizeof(sem_t));
    AssertAlways(err == 0);
}

OS_Handle
OS_SemaphoreOpen(String8 name)
{
    NotImplemented;
}

void
OS_SemaphoreClose(OS_Handle semaphore)
{
    NotImplemented;
}

B32
OS_SemaphoreTake(OS_Handle semaphore, U64 endt_us)
{
    AssertAlways(endt_us == max_U64 || endt_us == 0);

    if (endt_us == 0)
    {
        int err = sem_trywait((sem_t*)semaphore.u64[0]);
        if (err == 0)
        {
            return 1;
        }
        if (errno == EAGAIN)
        {
            return 0;
        }
        InvalidPath;
        return 0;
    }

    for (;;)
    {
        int err = sem_wait((sem_t*)semaphore.u64[0]);
        if (err == 0)
        {
            return 1;
        }
        if (errno == EINTR)
        {
            continue;
        }
        InvalidPath;
        return 0;
    }
}

void
OS_SemaphoreDrop(OS_Handle semaphore)
{
    for (;;)
    {
        int err = sem_post((sem_t*)semaphore.u64[0]);
        if (err == 0)
        {
            break;
        }
        else
        {
            if (errno == EAGAIN)
            {
                continue;
            }
        }
        InvalidPath;
        break;
    }
}

force_inline lib_internal U64
OS_SystemTimerFreqGet()
{
    return 1000000;
}
force_inline lib_internal U64
OS_SystemTimerRead()
{
    struct timeval Value;
    gettimeofday(&Value, 0);

    U64 Result = os_cpu_timer_read() * (U64)Value.tv_sec + (U64)Value.tv_usec;
    return Result;
}
////////////////////////////////
//~ rjf: @os_hooks Dynamically-Loaded Libraries (Implemented Per-OS)

OS_Handle
os_library_open(String8 path)
{
    Temp scratch = ScratchBegin(0, 0);
    char* path_cstr = (char*)push_str8_copy(scratch.arena, path).str;
    void* so = dlopen(path_cstr, RTLD_LAZY | RTLD_LOCAL);
    OS_Handle lib = {(U64)so};
    ScratchEnd(scratch);
    return lib;
}

VoidProc*
os_library_load_proc(OS_Handle lib, String8 name)
{
    Temp scratch = ScratchBegin(0, 0);
    void* so = (void*)lib.u64;
    char* name_cstr = (char*)push_str8_copy(scratch.arena, name).str;
    VoidProc* proc = (VoidProc*)dlsym(so, name_cstr);
    ScratchEnd(scratch);
    return proc;
}

void
os_library_close(OS_Handle lib)
{
    void* so = (void*)lib.u64;
    dlclose(so);
}

////////////////////////////////
//~ rjf: @os_hooks Safe Calls (Implemented Per-OS)

void
os_safe_call(OS_ThreadFunctionType* func, OS_ThreadFunctionType* fail_handler, void* ptr)
{
    // rjf: push handler to chain
    OS_LNX_SafeCallChain chain = {0};
    SLLStackPush(os_lnx_safe_call_chain, &chain);
    defer(SLLStackPop(os_lnx_safe_call_chain));
    chain.fail_handler = fail_handler;
    chain.ptr = ptr;

    // rjf: set up sig handler info
    struct sigaction new_act = {0};
    new_act.sa_sigaction = _os_lnx_safe_call_sig_handler;
    new_act.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigfillset(&new_act.sa_mask);
    int signals_to_handle[] = {
        SIGILL, SIGFPE, SIGSEGV, SIGBUS, SIGTRAP,
    };
    struct sigaction og_act[ArrayCount(signals_to_handle)] = {0};

    // rjf: attach handler info for all signals
    for (U32 i = 0; i < ArrayCount(signals_to_handle); i += 1)
    {
        sigaction(signals_to_handle[i], &new_act, &og_act[i]);
    }

    // rjf: call function
    func(ptr);

    // rjf: reset handler info for all signals
    for (U32 i = 0; i < ArrayCount(signals_to_handle); i += 1)
    {
        sigaction(signals_to_handle[i], &og_act[i], 0);
    }
}

////////////////////////////////
//~ rjf: @os_hooks GUIDs (Implemented Per-OS)

Guid
os_make_guid()
{
    Guid guid = {0};
    getrandom(guid.v, sizeof(guid.v), 0);
    guid.data3 &= 0x0fff;
    guid.data3 |= (4 << 12);
    guid.data4[0] &= 0x3f;
    guid.data4[0] |= 0x80;
    return guid;
}
////////////////////////////////
//~ mgj: @os_hooks OS Graphical Message
void
os_graphical_message(B32 error, String8 title, String8 message)
{
    if (error)
    {
        fprintf(stderr, "[X] ");
    }
    fprintf(stderr, "%.*s\n", str8_varg(title));
    fprintf(stderr, "%.*s\n\n", str8_varg(message));
}
////////////////////////////////
//~ rjf: @os_hooks Entry Points (Implemented Per-OS)

int
main(int argc, char** argv)
{
    // Resolve the optional symbolizer and load unwinding support before a crash.
    const char* search_path = getenv("PATH");
    if (search_path)
    {
        for (const char* directory = search_path;;)
        {
            const char* end = strchr(directory, ':');
            U64 directory_size = 0;
            if (end)
                directory_size = (U64)(end - directory);
            else
                directory_size = strlen(directory);
            int path_size =
                snprintf(os_lnx_state.crash_symbolizer_path, sizeof(os_lnx_state.crash_symbolizer_path),
                         "%.*s%sllvm-symbolizer", (int)directory_size, directory, directory_size ? "/" : "");
            if (path_size > 0 && (U64)path_size < sizeof(os_lnx_state.crash_symbolizer_path))
            {
                int executable = access(os_lnx_state.crash_symbolizer_path, X_OK);
                if (executable == 0)
                    break;
            }
            os_lnx_state.crash_symbolizer_path[0] = 0;
            if (!end)
                break;
            directory = end + 1;
        }
    }
    void* preload_frames[1];
    backtrace(preload_frames, ArrayCount(preload_frames));
    _os_lnx_crash_thread_init();

    // Install once for the process; synchronous faults run on the failing thread.
    struct sigaction crash_handler = {};
    crash_handler.sa_sigaction = _os_lnx_crash_signal_handler;
    crash_handler.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigfillset(&crash_handler.sa_mask);
    const int crash_signals[] = {SIGILL, SIGTRAP, SIGABRT, SIGFPE, SIGBUS, SIGSEGV, SIGQUIT};
    for (int signal_number : crash_signals)
    {
#if ASAN_ENABLED
        // Keep sanitizer diagnostics for signals the sanitizer already handles.
        struct sigaction existing_handler = {};
        sigaction(signal_number, 0, &existing_handler);
        if (existing_handler.sa_handler != SIG_DFL)
            continue;
#endif
        sigaction(signal_number, &crash_handler, 0);
    }

    //- rjf: set up OS layer
    {
        //- rjf: get statically-allocated system/process info
        {
            OS_SystemInfo* info = &os_lnx_state.system_info;
            info->logical_processor_count = (U32)get_nprocs();
            info->page_size = (U64)getpagesize();
            info->large_page_size = MB(2);
            info->allocation_granularity = info->page_size;
        }
        {
            OS_ProcessInfo* info = &os_lnx_state.process_info;
            info->pid = (U32)getpid();
        }

        //- rjf: set up thread context
        local_persist TCTX tctx;
        TCTX_InitAndEquip(&tctx);

        //- rjf: set up dynamically allocated state
        os_lnx_state.arena = arena_alloc();
        Debug_SetName(os_lnx_state.arena, "linux OS state arena");
        os_lnx_state.entity_arena = arena_alloc();
        Debug_SetName(os_lnx_state.entity_arena, "linux OS entity arena");
        pthread_mutex_init(&os_lnx_state.entity_mutex, 0);

        //- rjf: grab dynamically allocated system info
        {
            Temp scratch = ScratchBegin(0, 0);
            OS_SystemInfo* info = &os_lnx_state.system_info;

            // rjf: get machine name
            B32 got_final_result = 0;
            U8* buffer = 0;
            int size = 0;
            for (S64 cap = 4096, r = 0; r < 4; cap *= 2, r += 1)
            {
                ScratchEnd(scratch);
                buffer = PushArrayNoZero(scratch.arena, U8, cap);
                size = gethostname((char*)buffer, cap);
                if (size < cap)
                {
                    got_final_result = 1;
                    break;
                }
            }

            // rjf: save name to info
            if (got_final_result && size > 0)
            {
                info->machine_name.size = size;
                info->machine_name.str = PushArrayNoZero(os_lnx_state.arena, U8, info->machine_name.size + 1);
                MemoryCopy(info->machine_name.str, buffer, info->machine_name.size);
                info->machine_name.str[info->machine_name.size] = 0;
            }

            ScratchEnd(scratch);
        }

        //- rjf: grab dynamically allocated process info
        {
            Temp scratch = ScratchBegin(0, 0);
            OS_ProcessInfo* info = &os_lnx_state.process_info;

            // rjf: grab binary path
            {
                // rjf: get self string
                B32 got_final_result = 0;
                U8* buffer = 0;
                int size = 0;
                for (S64 cap = PATH_MAX, r = 0; r < 4; cap *= 2, r += 1)
                {
                    ScratchEnd(scratch);
                    buffer = PushArrayNoZero(scratch.arena, U8, cap);
                    size = readlink("/proc/self/exe", (char*)buffer, cap);
                    if (size < cap)
                    {
                        got_final_result = 1;
                        break;
                    }
                }

                // rjf: save
                if (got_final_result && size > 0)
                {
                    String8 full_name = str8(buffer, size);
                    String8 name_chopped = str8_chop_last_slash(full_name);
                    info->binary_path = push_str8_copy(os_lnx_state.arena, name_chopped);
                }
            }

            // rjf: grab initial directory
            {
                info->initial_path = os_current_path_get(os_lnx_state.arena);
            }

            // rjf: grab home directory
            {
                char* home = getenv("HOME");
                info->user_program_data_path = str8_c_string(home);
            }

            ScratchEnd(scratch);
        }
    }

    //- rjf: call into "real" entry point
    return App(argc, argv);
}

////////////////////////////////
//~ mgj: Private Crash Reporting Helpers

lib_internal void
_os_lnx_crash_thread_init()
{
    // Alternate stacks are per-thread. Keep them outside arenas so a damaged
    // arena or exhausted application stack does not prevent crash reporting.
    local_persist thread_static U8 signal_stack[KB(64)];
    stack_t stack = {};
    sigaltstack(0, &stack);
    if (!(stack.ss_flags & SS_DISABLE))
        return;
    stack.ss_sp = signal_stack;
    stack.ss_size = sizeof(signal_stack);
    stack.ss_flags = 0;
    sigaltstack(&stack, 0);
}

lib_internal void
_os_lnx_crash_write(const char* text, U64 size)
{
    while (size > 0)
    {
        ssize_t written = write(STDERR_FILENO, text, size);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            break;
        text += written;
        size -= written;
    }
}

lib_internal void
_os_lnx_crash_reraise(int signal_number)
{
    // Preserve signal termination and core dumps instead of converting the crash
    // to a normal exit code. This also handles recursive/concurrent failures.
    struct sigaction default_handler = {};
    default_handler.sa_handler = SIG_DFL;
    sigemptyset(&default_handler.sa_mask);
    sigaction(signal_number, &default_handler, 0);
    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, signal_number);
    sigprocmask(SIG_UNBLOCK, &signal_set, 0);
    raise(signal_number);
    _exit(128 + signal_number);
}

lib_internal void
_os_lnx_crash_frame_symbolize(const char* module_path, U64 module_offset, U64 deadline_us)
{
    // As in RAD, symbolization is best effort after a fatal fault. Use direct
    // arguments so spaces and shell metacharacters in paths stay literal.
    int output_pipe[2];
    int pipe_result = pipe2(output_pipe, O_CLOEXEC);
    if (pipe_result != 0)
        return;
    defer(close(output_pipe[0]));

    pid_t symbolizer_pid = 0;
    {
        defer(close(output_pipe[1]));
        posix_spawn_file_actions_t actions;
        int actions_result = posix_spawn_file_actions_init(&actions);
        if (actions_result != 0)
            return;
        defer(posix_spawn_file_actions_destroy(&actions));
        posix_spawnattr_t attributes;
        int attributes_result = posix_spawnattr_init(&attributes);
        if (attributes_result != 0)
            return;
        defer(posix_spawnattr_destroy(&attributes));

        sigset_t child_mask;
        sigemptyset(&child_mask);
        int setup_result = posix_spawnattr_setsigmask(&attributes, &child_mask);
        setup_result |= posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK);
        setup_result |= posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
        setup_result |= posix_spawn_file_actions_adddup2(&actions, output_pipe[1], STDOUT_FILENO);
        setup_result |= posix_spawn_file_actions_adddup2(&actions, output_pipe[1], STDERR_FILENO);
        setup_result |= posix_spawn_file_actions_addclose(&actions, output_pipe[0]);
        setup_result |= posix_spawn_file_actions_addclose(&actions, output_pipe[1]);
        if (setup_result != 0)
            return;

        char address[32];
        snprintf(address, sizeof(address), "0x%llx", (unsigned long long)module_offset);
        char* arguments[] = {os_lnx_state.crash_symbolizer_path,
                             (char*)"--relative-address",
                             (char*)"--functions",
                             (char*)"--demangle",
                             (char*)"--no-debuginfod",
                             (char*)"--obj",
                             (char*)module_path,
                             address,
                             0};
        int spawn_result = posix_spawn(&symbolizer_pid, arguments[0], &actions, &attributes, arguments, environ);
        if (spawn_result != 0)
            return;
    }
    // All waits are confined to fatal reporting. Bound the entire report's
    // symbolization time; an absent or stalled tool must not hide raw frames.
    defer({
        kill(symbolizer_pid, SIGKILL);
        int status;
        pid_t joined;
        do
        {
            joined = waitpid(symbolizer_pid, &status, 0);
        } while (joined < 0 && errno == EINTR);
    });
    for (;;)
    {
        U64 now_us = os_now_microseconds();
        if (now_us >= deadline_us)
            break;
        int timeout_ms = (int)((deadline_us - now_us + 999) / 1000);
        pollfd output = {output_pipe[0], POLLIN, 0};
        int poll_result = poll(&output, 1, timeout_ms);
        if (poll_result < 0 && errno == EINTR)
            continue;
        if (poll_result <= 0 || !(output.revents & (POLLIN | POLLHUP)))
            break;
        char text[512];
        ssize_t text_size = read(output_pipe[0], text, sizeof(text));
        if (text_size < 0 && errno == EINTR)
            continue;
        if (text_size <= 0)
            break;
        _os_lnx_crash_write(text, (U64)text_size);
    }
}

lib_internal void
_os_lnx_crash_signal_handler(int signal_number, siginfo_t* signal_info, void* context)
{
    U32 already_reporting = ins_atomic_u32_eval_assign(&os_lnx_state.crash_report_started, 1);
    if (already_reporting)
        _os_lnx_crash_reraise(signal_number);

    const char* signal_name = "unknown signal";
    switch (signal_number)
    {
        case SIGILL: signal_name = "SIGILL"; break;
        case SIGTRAP: signal_name = "SIGTRAP"; break;
        case SIGABRT: signal_name = "SIGABRT"; break;
        case SIGFPE: signal_name = "SIGFPE"; break;
        case SIGBUS: signal_name = "SIGBUS"; break;
        case SIGSEGV: signal_name = "SIGSEGV"; break;
        case SIGQUIT: signal_name = "SIGQUIT"; break;
    }
    void* fault_address = 0;
    if (signal_info->si_code > 0)
        fault_address = signal_info->si_addr;
    U64 instruction_address = 0;
    ucontext_t* signal_context = (ucontext_t*)context;
#if ARCH_X64
    instruction_address = (U64)signal_context->uc_mcontext.gregs[REG_RIP];
#elif ARCH_X86
    instruction_address = (U64)signal_context->uc_mcontext.gregs[REG_EIP];
#elif ARCH_ARM64
    instruction_address = (U64)signal_context->uc_mcontext.pc;
#elif ARCH_ARM32
    instruction_address = (U64)signal_context->uc_mcontext.arm_pc;
#endif
    char text[PATH_MAX + 256];
    int text_size =
        snprintf(text, sizeof(text),
                 "\n--- Fatal Signal ---\nA fatal signal was received: %s (%d). The process is terminating.\n"
                 "Fault address: %p\nInstruction address: 0x%llx\nCallstack:\n",
                 signal_name, signal_number, fault_address, (unsigned long long)instruction_address);
    _os_lnx_crash_write(text, (U64)text_size);

    // Fixed buffers avoid arena allocation in the damaged process. Unwinding,
    // dladdr, formatting, and spawning are best effort, not async-signal-safe.
    void* frames[64];
    int frame_count = backtrace(frames, ArrayCount(frames));
    U64 started_us = os_now_microseconds();
    U64 symbolizer_deadline_us = started_us + (U64)Million(3);
    for (int frame_idx = 0; frame_idx < frame_count; ++frame_idx)
    {
        Dl_info module = {};
        int module_found = dladdr(frames[frame_idx], &module);
        const char* module_path = module_found && module.dli_fname ? module.dli_fname : "<unknown module>";
        U64 module_offset = (U64)frames[frame_idx] - (U64)module.dli_fbase;
        text_size = snprintf(text, sizeof(text), "%d. [%p] %s +0x%llx\n", frame_idx + 1, frames[frame_idx], module_path,
                             (unsigned long long)module_offset);
        U64 write_size = Min((U64)text_size, sizeof(text) - 1);
        _os_lnx_crash_write(text, write_size);
        U64 now_us = os_now_microseconds();
        if (module_found && os_lnx_state.crash_symbolizer_path[0] && now_us < symbolizer_deadline_us)
            _os_lnx_crash_frame_symbolize(module_path, module_offset, symbolizer_deadline_us);
    }
    if (!os_lnx_state.crash_symbolizer_path[0])
    {
        const char unavailable[] = "llvm-symbolizer unavailable; showing module addresses.\n";
        _os_lnx_crash_write(unavailable, sizeof(unavailable) - 1);
    }
    const char version[] = "\nVersion: " BUILD_VERSION_STRING_LITERAL BUILD_GIT_HASH_STRING_LITERAL_APPEND "\n";
    _os_lnx_crash_write(version, sizeof(version) - 1);
    _os_lnx_crash_reraise(signal_number);
}

lib_internal void
_os_lnx_safe_call_sig_handler(int signal_number, siginfo_t* signal_info, void* context)
{
    OS_LNX_SafeCallChain* chain = os_lnx_safe_call_chain;
    if (chain != 0 && chain->fail_handler != 0)
        chain->fail_handler(chain->ptr);
    _os_lnx_crash_signal_handler(signal_number, signal_info, context);
}
