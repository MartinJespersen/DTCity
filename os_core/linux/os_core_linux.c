// Copyright (c) 2024 Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Helpers

static DateTime os_lnx_date_time_from_tm(tm in, U32 msec) {
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

static tm os_lnx_tm_from_date_time(DateTime dt) {
  tm result = {0};
  result.tm_sec = dt.sec;
  result.tm_min = dt.min;
  result.tm_hour = dt.hour;
  result.tm_mday = dt.day + 1;
  result.tm_mon = dt.mon;
  result.tm_year = dt.year - 1900;
  return result;
}

static timespec os_lnx_timespec_from_date_time(DateTime dt) {
  tm tm_val = os_lnx_tm_from_date_time(dt);
  time_t seconds = timegm(&tm_val);
  timespec result = {0};
  result.tv_sec = seconds;
  return result;
}

static DenseTime os_lnx_dense_time_from_timespec(timespec in) {
  DenseTime result = 0;
  {
    struct tm tm_time = {0};
    gmtime_r(&in.tv_sec, &tm_time);
    DateTime date_time =
        os_lnx_date_time_from_tm(tm_time, in.tv_nsec / Million(1));
    result = dense_time_from_date_time(date_time);
  }
  return result;
}

static FileProperties os_lnx_file_properties_from_stat(struct stat *s) {
  FileProperties props = {0};
  props.size = s->st_size;
  props.created = os_lnx_dense_time_from_timespec(s->st_ctim);
  props.modified = os_lnx_dense_time_from_timespec(s->st_mtim);
  if (s->st_mode & S_IFDIR) {
    props.flags |= FilePropertyFlag_IsFolder;
  }
  return props;
}

static void os_lnx_safe_call_sig_handler(int x) {
  OS_LNX_SafeCallChain *chain = os_lnx_safe_call_chain;
  if (chain != 0 && chain->fail_handler != 0) {
    chain->fail_handler(chain->ptr);
  }
  abort();
}

////////////////////////////////
//~ rjf: Entities

static OS_LNX_Entity *os_lnx_entity_alloc(OS_LNX_EntityKind kind) {
  OS_LNX_Entity *entity = 0;
  DeferLoop(pthread_mutex_lock(&os_lnx_state.entity_mutex),
            pthread_mutex_unlock(&os_lnx_state.entity_mutex)) {
    entity = os_lnx_state.entity_free;
    if (entity) {
      SLLStackPop(os_lnx_state.entity_free);
    } else {
      entity = PushArrayNoZero(os_lnx_state.entity_arena, OS_LNX_Entity, 1);
    }
  }
  MemoryZeroStruct(entity);
  entity->kind = kind;
  return entity;
}

static void os_lnx_entity_release(OS_LNX_Entity *entity) {
  DeferLoop(pthread_mutex_lock(&os_lnx_state.entity_mutex),
            pthread_mutex_unlock(&os_lnx_state.entity_mutex)) {
    SLLStackPush(os_lnx_state.entity_free, entity);
  }
}

////////////////////////////////
//~ rjf: Thread Entry Point

static void *os_lnx_thread_entry_point(void *ptr) {
  OS_LNX_Entity *entity = (OS_LNX_Entity *)ptr;
  OS_ThreadFunctionType *func = entity->thread.func;
  void *thread_ptr = entity->thread.ptr;
  TCTX tctx_;
  tctx_init_and_equip(&tctx_);
  func(thread_ptr);
  tctx_release();
  return 0;
}

////////////////////////////////
//~ rjf: @os_hooks System/Process Info (Implemented Per-OS)

static OS_SystemInfo *OS_GetSystemInfo(void) {
  return &os_lnx_state.system_info;
}

static OS_ProcessInfo *os_get_process_info(void) {
  return &os_lnx_state.process_info;
}

static String8 OS_GetCurrentPath(Arena *arena) {
  char *cwdir = getcwd(0, 0);
  String8 string = PushStr8Copy(arena, Str8CString(cwdir));
  free(cwdir);
  return string;
}

static inline String8 OS_PathDelimiter(void) { return Str8Lit("/"); }

static U32 os_get_process_start_time_unix(void) {
  Temp scratch = ScratchBegin(0, 0);
  U64 start_time = 0;
  pid_t pid = getpid();
  String8 path = push_str8f(scratch.arena, "/proc/%u", pid);
  struct stat st;
  int err = stat((char *)path.str, &st);
  if (err == 0) {
    start_time = st.st_mtime;
  }
  ScratchEnd(scratch);
  return (U32)start_time;
}

////////////////////////////////
//~ rjf: @os_hooks Memory Allocation (Implemented Per-OS)

//- rjf: basic

static void *os_reserve(U64 size) {
  void *result = mmap(0, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (result == MAP_FAILED) {
    result = 0;
  }
  return result;
}

static B32 os_commit(void *ptr, U64 size) {
  mprotect(ptr, size, PROT_READ | PROT_WRITE);
  return 1;
}

static void os_decommit(void *ptr, U64 size) {
  madvise(ptr, size, MADV_DONTNEED);
  mprotect(ptr, size, PROT_NONE);
}

static void os_release(void *ptr, U64 size) { munmap(ptr, size); }

//- rjf: large pages

static void *os_reserve_large(U64 size) {
  void *result = mmap(0, size, PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
  if (result == MAP_FAILED) {
    result = 0;
  }
  return result;
}

static B32 os_commit_large(void *ptr, U64 size) {
  mprotect(ptr, size, PROT_READ | PROT_WRITE);
  return 1;
}

////////////////////////////////
//~ rjf: @os_hooks Thread Info (Implemented Per-OS)

static U32 os_tid(void) {
  U32 result = gettid();
  return result;
}

static void OS_SetThreadName(String8 name) {
  Temp scratch = ScratchBegin(0, 0);
  String8 name_copy = PushStr8Copy(scratch.arena, name);
  pthread_t current_thread = pthread_self();
  pthread_setname_np(current_thread, (char *)name_copy.str);
  ScratchEnd(scratch);
}

////////////////////////////////
//~ rjf: @os_hooks Aborting (Implemented Per-OS)

static void os_abort(S32 exit_code) { exit(exit_code); }

////////////////////////////////
//~ rjf: @os_hooks File System (Implemented Per-OS)

//- rjf: files

static OS_Handle os_file_open(OS_AccessFlags flags, String8 path) {
  Temp scratch = ScratchBegin(0, 0);
  String8 path_copy = PushStr8Copy(scratch.arena, path);
  int lnx_flags = 0;
  if (flags & OS_AccessFlag_Read && flags & OS_AccessFlag_Write) {
    lnx_flags = O_RDWR;
  } else if (flags & OS_AccessFlag_Write) {
    lnx_flags = O_WRONLY;
  } else if (flags & OS_AccessFlag_Read) {
    lnx_flags = O_RDONLY;
  }
  if (flags & OS_AccessFlag_Append) {
    lnx_flags |= O_APPEND;
  }
  if (flags & (OS_AccessFlag_Write | OS_AccessFlag_Append)) {
    lnx_flags |= O_CREAT;
  }
  int fd = open((char *)path_copy.str, lnx_flags, 0755);
  OS_Handle handle = {0};
  if (fd != -1) {
    handle.u64[0] = fd;
  }
  ScratchEnd(scratch);
  return handle;
}

static void os_file_close(OS_Handle file) {
  if (OS_HandleMatch(file, OS_HandleIsZero())) {
    return;
  }
  int fd = (int)file.u64[0];
  close(fd);
}

static U64 os_file_read(OS_Handle file, Rng1U64 rng, void *out_data) {
  if (OS_HandleMatch(file, OS_HandleIsZero())) {
    return 0;
  }
  int fd = (int)file.u64[0];
  U64 total_num_bytes_to_read = dim_1u64(rng);
  U64 total_num_bytes_read = 0;
  U64 total_num_bytes_left_to_read = total_num_bytes_to_read;
  for (; total_num_bytes_left_to_read > 0;) {
    int read_result =
        pread(fd, (U8 *)out_data + total_num_bytes_read,
              total_num_bytes_left_to_read, rng.min + total_num_bytes_read);
    if (read_result >= 0) {
      total_num_bytes_read += read_result;
      total_num_bytes_left_to_read -= read_result;
    } else if (errno != EINTR) {
      break;
    }
  }
  return total_num_bytes_read;
}

static U64 os_file_write(OS_Handle file, Rng1U64 rng, void *data) {
  if (OS_HandleMatch(file, OS_HandleIsZero())) {
    return 0;
  }
  int fd = (int)file.u64[0];
  U64 total_num_bytes_to_write = dim_1u64(rng);
  U64 total_num_bytes_written = 0;
  U64 total_num_bytes_left_to_write = total_num_bytes_to_write;
  for (; total_num_bytes_left_to_write > 0;) {
    int write_result = pwrite(fd, (U8 *)data + total_num_bytes_written,
                              total_num_bytes_left_to_write,
                              rng.min + total_num_bytes_written);
    if (write_result >= 0) {
      total_num_bytes_written += write_result;
      total_num_bytes_left_to_write -= write_result;
    } else if (errno != EINTR) {
      break;
    }
  }
  return total_num_bytes_written;
}

static B32 os_file_set_times(OS_Handle file, DateTime date_time) {
  if (OS_HandleMatch(file, OS_HandleIsZero())) {
    return 0;
  }
  int fd = (int)file.u64[0];
  timespec time = os_lnx_timespec_from_date_time(date_time);
  timespec times[2] = {time, time};
  int futimens_result = futimens(fd, times);
  B32 good = (futimens_result != -1);
  return good;
}

static FileProperties os_properties_from_file(OS_Handle file) {
  if (OS_HandleMatch(file, OS_HandleIsZero())) {
    return (FileProperties){0};
  }
  int fd = (int)file.u64[0];
  struct stat fd_stat = {0};
  int fstat_result = fstat(fd, &fd_stat);
  FileProperties props = {0};
  if (fstat_result != -1) {
    props = os_lnx_file_properties_from_stat(&fd_stat);
  }
  return props;
}

static OS_FileID os_id_from_file(OS_Handle file) {
  if (OS_HandleMatch(file, OS_HandleIsZero())) {
    return (OS_FileID){0};
  }
  int fd = (int)file.u64[0];
  struct stat fd_stat = {0};
  int fstat_result = fstat(fd, &fd_stat);
  OS_FileID id = {0};
  if (fstat_result != -1) {
    id.v[0] = fd_stat.st_dev;
    id.v[1] = fd_stat.st_ino;
  }
  return id;
}

static B32 os_delete_file_at_path(String8 path) {
  Temp scratch = ScratchBegin(0, 0);
  B32 result = 0;
  String8 path_copy = PushStr8Copy(scratch.arena, path);
  if (remove((char *)path_copy.str) != -1) {
    result = 1;
  }
  ScratchEnd(scratch);
  return result;
}

static B32 os_copy_file_path(String8 dst, String8 src) {
  B32 result = 0;
  OS_Handle src_h = os_file_open(OS_AccessFlag_Read, src);
  OS_Handle dst_h = os_file_open(OS_AccessFlag_Write, dst);
  if (!OS_HandleMatch(src_h, OS_HandleIsZero()) &&
      !OS_HandleMatch(dst_h, OS_HandleIsZero())) {
    int src_fd = (int)src_h.u64[0];
    int dst_fd = (int)dst_h.u64[0];
    FileProperties src_props = os_properties_from_file(src_h);
    U64 size = src_props.size;
    U64 total_bytes_copied = 0;
    U64 bytes_left_to_copy = size;
    for (; bytes_left_to_copy > 0;) {
      off_t sendfile_off = total_bytes_copied;
      int send_result =
          sendfile(dst_fd, src_fd, &sendfile_off, bytes_left_to_copy);
      if (send_result <= 0) {
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

static B32 os_move_file_path(String8 dst, String8 src) {
  // TODO(rjf)
}

static String8 os_full_path_from_path(Arena *arena, String8 path) {
  Temp scratch = ScratchBegin(&arena, 1);
  String8 path_copy = PushStr8Copy(scratch.arena, path);
  char buffer[PATH_MAX] = {0};
  realpath((char *)path_copy.str, buffer);
  String8 result = PushStr8Copy(arena, Str8CString(buffer));
  ScratchEnd(scratch);
  return result;
}

static B32 os_file_path_exists(String8 path) {
  Temp scratch = ScratchBegin(0, 0);
  String8 path_copy = PushStr8Copy(scratch.arena, path);
  int access_result = access((char *)path_copy.str, F_OK);
  B32 result = 0;
  if (access_result == 0) {
    result = 1;
  }
  ScratchEnd(scratch);
  return result;
}

static B32 os_folder_path_exists(String8 path) {
  Temp scratch = ScratchBegin(0, 0);
  B32 exists = 0;
  String8 path_copy = PushStr8Copy(scratch.arena, path);
  DIR *handle = opendir((char *)path_copy.str);
  if (handle) {
    closedir(handle);
    exists = 1;
  }
  ScratchEnd(scratch);
  return exists;
}

static FileProperties os_properties_from_file_path(String8 path) {
  Temp scratch = ScratchBegin(0, 0);
  String8 path_copy = PushStr8Copy(scratch.arena, path);
  struct stat f_stat = {0};
  int stat_result = stat((char *)path_copy.str, &f_stat);
  FileProperties props = {0};
  if (stat_result != -1) {
    props = os_lnx_file_properties_from_stat(&f_stat);
  }
  ScratchEnd(scratch);
  return props;
}

//- rjf: file maps

static OS_Handle os_file_map_open(OS_AccessFlags flags, OS_Handle file) {
  OS_Handle map = file;
  return map;
}

static void os_file_map_close(OS_Handle map) {
  // NOTE(rjf): nothing to do; `map` handles are the same as `file` handles
  // in
  // the linux implementation (on Windows they require separate handles)
}

static void *os_file_map_view_open(OS_Handle map, OS_AccessFlags flags,
                                   Rng1U64 range) {
  if (OS_HandleMatch(map, OS_HandleIsZero())) {
    return 0;
  }
  int fd = (int)map.u64[0];
  int prot_flags = 0;
  if (flags & OS_AccessFlag_Write) {
    prot_flags |= PROT_WRITE;
  }
  if (flags & OS_AccessFlag_Read) {
    prot_flags |= PROT_READ;
  }
  int map_flags = MAP_PRIVATE;
  void *base = mmap(0, dim_1u64(range), prot_flags, map_flags, fd, range.min);
  if (base == MAP_FAILED) {
    base = 0;
  }
  return base;
}

static void os_file_map_view_close(OS_Handle map, void *ptr, Rng1U64 range) {
  munmap(ptr, dim_1u64(range));
}

//- rjf: directory iteration

static OS_FileIter *os_file_iter_begin(Arena *arena, String8 path,
                                       OS_FileIterFlags flags) {
  OS_FileIter *base_iter = PushArray(arena, OS_FileIter, 1);
  base_iter->flags = flags;
  OS_LNX_FileIter *iter = (OS_LNX_FileIter *)base_iter->memory;
  {
    String8 path_copy = PushStr8Copy(arena, path);
    iter->dir = opendir((char *)path_copy.str);
    iter->path = path_copy;
  }
  return base_iter;
}

static B32 os_file_iter_next(Arena *arena, OS_FileIter *iter,
                             OS_FileInfo *info_out) {
  B32 good = 0;
  OS_LNX_FileIter *lnx_iter = (OS_LNX_FileIter *)iter->memory;
  for (;;) {
    // rjf: get next entry
    lnx_iter->dp = readdir(lnx_iter->dir);
    good = (lnx_iter->dp != 0);

    // rjf: unpack entry info
    struct stat st = {0};
    int stat_result = 0;
    if (good) {
      Temp scratch = ScratchBegin(&arena, 1);
      String8 full_path = push_str8f(scratch.arena, "%S/%s", lnx_iter->path,
                                     lnx_iter->dp->d_name);
      stat_result = stat((char *)full_path.str, &st);
      ScratchEnd(scratch);
    }

    // rjf: determine if filtered
    B32 filtered = 0;
    if (good) {
      filtered =
          ((st.st_mode == S_IFDIR &&
            iter->flags & OS_FileIterFlag_SkipFolders) ||
           (st.st_mode == S_IFREG && iter->flags & OS_FileIterFlag_SkipFiles) ||
           (lnx_iter->dp->d_name[0] == '.' && lnx_iter->dp->d_name[1] == 0) ||
           (lnx_iter->dp->d_name[0] == '.' && lnx_iter->dp->d_name[1] == '.' &&
            lnx_iter->dp->d_name[2] == 0));
    }

    // rjf: output & exit, if good & unfiltered
    if (good && !filtered) {
      info_out->name = PushStr8Copy(arena, Str8CString(lnx_iter->dp->d_name));
      if (stat_result != -1) {
        info_out->props = os_lnx_file_properties_from_stat(&st);
      }
      break;
    }

    // rjf: exit if not good
    if (!good) {
      break;
    }
  }
  return good;
}

static void os_file_iter_end(OS_FileIter *iter) {
  OS_LNX_FileIter *lnx_iter = (OS_LNX_FileIter *)iter->memory;
  closedir(lnx_iter->dir);
}

//- rjf: directory creation

static B32 os_make_directory(String8 path) {
  Temp scratch = ScratchBegin(0, 0);
  B32 result = 0;
  String8 path_copy = PushStr8Copy(scratch.arena, path);
  if (mkdir((char *)path_copy.str, 0755) != -1) {
    result = 1;
  }
  ScratchEnd(scratch);
  return result;
}

////////////////////////////////
//~ rjf: @os_hooks Shared Memory (Implemented Per-OS)

static OS_Handle os_shared_memory_alloc(U64 size, String8 name) {
  Temp scratch = ScratchBegin(0, 0);
  String8 name_copy = PushStr8Copy(scratch.arena, name);
  int id = shm_open((char *)name_copy.str, O_RDWR, 0);
  ftruncate(id, size);
  OS_Handle result = {(U64)id};
  ScratchEnd(scratch);
  return result;
}

static OS_Handle os_shared_memory_open(String8 name) {
  Temp scratch = ScratchBegin(0, 0);
  String8 name_copy = PushStr8Copy(scratch.arena, name);
  int id = shm_open((char *)name_copy.str, O_RDWR, 0);
  OS_Handle result = {(U64)id};
  ScratchEnd(scratch);
  return result;
}

static void os_shared_memory_close(OS_Handle handle) {
  if (OS_HandleMatch(handle, OS_HandleIsZero())) {
    return;
  }
  int id = (int)handle.u64[0];
  close(id);
}

static void *os_shared_memory_view_open(OS_Handle handle, Rng1U64 range) {
  if (OS_HandleMatch(handle, OS_HandleIsZero())) {
    return 0;
  }
  int id = (int)handle.u64[0];
  void *base = mmap(0, dim_1u64(range), PROT_READ | PROT_WRITE, MAP_SHARED, id,
                    range.min);
  if (base == MAP_FAILED) {
    base = 0;
  }
  return base;
}

static void os_shared_memory_view_close(OS_Handle handle, void *ptr,
                                        Rng1U64 range) {
  if (OS_HandleMatch(handle, OS_HandleIsZero())) {
    return;
  }
  munmap(ptr, dim_1u64(range));
}

////////////////////////////////
//~ rjf: @os_hooks Time (Implemented Per-OS)

static U64 os_now_microseconds(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  U64 result = t.tv_sec * Million(1) + (t.tv_nsec / Thousand(1));
  return result;
}

static U32 os_now_unix(void) {
  time_t t = time(0);
  return (U32)t;
}

static DateTime os_now_universal_time(void) {
  time_t t = 0;
  time(&t);
  struct tm universal_tm = {0};
  gmtime_r(&t, &universal_tm);
  DateTime result = os_lnx_date_time_from_tm(universal_tm, 0);
  return result;
}

static DateTime os_universal_time_from_local(DateTime *date_time) {
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

static DateTime os_local_time_from_universal(DateTime *date_time) {
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

static void os_sleep_milliseconds(U32 msec) { usleep(msec * Thousand(1)); }

////////////////////////////////
//~ rjf: @os_hooks Child Processes (Implemented Per-OS)

static OS_Handle os_process_launch(OS_ProcessLaunchParams *params) {
  NotImplemented;
}

static B32 os_process_join(OS_Handle handle, U64 endt_us) { NotImplemented; }

static void os_process_detach(OS_Handle handle) { NotImplemented; }

////////////////////////////////
//~ rjf: @os_hooks Threads (Implemented Per-OS)

static OS_Handle OS_ThreadLaunch(OS_ThreadFunctionType *func, void *ptr,
                                 void *params) {
  OS_LNX_Entity *entity = os_lnx_entity_alloc(OS_LNX_EntityKind_Thread);
  entity->thread.func = func;
  entity->thread.ptr = ptr;
  {
    int pthread_result = pthread_create(&entity->thread.handle, 0,
                                        os_lnx_thread_entry_point, entity);
    if (pthread_result == -1) {
      os_lnx_entity_release(entity);
      entity = 0;
    }
  }
  OS_Handle handle = {(U64)entity};
  return handle;
}

static B32 OS_ThreadJoin(OS_Handle handle, U64 endt_us) {
  if (OS_HandleMatch(handle, OS_HandleIsZero())) {
    return 0;
  }
  OS_LNX_Entity *entity = (OS_LNX_Entity *)handle.u64[0];
  int join_result = pthread_join(entity->thread.handle, 0);
  B32 result = (join_result == 0);
  os_lnx_entity_release(entity);
  return result;
}

static void os_thread_detach(OS_Handle handle) {
  if (OS_HandleMatch(handle, OS_HandleIsZero())) {
    return;
  }
  OS_LNX_Entity *entity = (OS_LNX_Entity *)handle.u64[0];
  os_lnx_entity_release(entity);
}

////////////////////////////////
//~ rjf: @os_hooks Synchronization Primitives (Implemented Per-OS)

//- rjf: mutexes

static OS_Handle OS_MutexAlloc(void) {
  OS_LNX_Entity *entity = os_lnx_entity_alloc(OS_LNX_EntityKind_Mutex);
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  int init_result = pthread_mutex_init(&entity->mutex_handle, &attr);
  pthread_mutexattr_destroy(&attr);
  if (init_result == -1) {
    os_lnx_entity_release(entity);
    entity = 0;
  }
  OS_Handle handle = {(U64)entity};
  return handle;
}
static void OS_MutexRelease(OS_Handle mutex) {
  if (OS_HandleMatch(mutex, OS_HandleIsZero())) {
    return;
  }
  OS_LNX_Entity *entity = (OS_LNX_Entity *)mutex.u64[0];
  pthread_mutex_destroy(&entity->mutex_handle);
  os_lnx_entity_release(entity);
}

static void OS_MutexTake(OS_Handle mutex) {
  if (OS_HandleMatch(mutex, OS_HandleIsZero())) {
    return;
  }
  OS_LNX_Entity *entity = (OS_LNX_Entity *)mutex.u64[0];
  pthread_mutex_lock(&entity->mutex_handle);
}

static void OS_MutexDrop(OS_Handle mutex) {
  if (OS_HandleMatch(mutex, OS_HandleIsZero())) {
    return;
  }
  OS_LNX_Entity *entity = (OS_LNX_Entity *)mutex.u64[0];
  pthread_mutex_unlock(&entity->mutex_handle);
}

//- rjf: reader/writer mutexes

static OS_Handle OS_RWMutexAlloc(void) {
  OS_LNX_Entity *entity = os_lnx_entity_alloc(OS_LNX_EntityKind_RWMutex);
  int init_result = pthread_rwlock_init(&entity->rwmutex_handle, 0);
  if (init_result == -1) {
    os_lnx_entity_release(entity);
    entity = 0;
  }
  OS_Handle handle = {(U64)entity};
  return handle;
}

static void OS_RWMutexRelease(OS_Handle rw_mutex) {
  if (OS_HandleMatch(rw_mutex, OS_HandleIsZero())) {
    return;
  }
  OS_LNX_Entity *entity = (OS_LNX_Entity *)rw_mutex.u64[0];
  pthread_rwlock_destroy(&entity->rwmutex_handle);
  os_lnx_entity_release(entity);
}

static void OS_RWMutexTakeR(OS_Handle rw_mutex) {
  if (OS_HandleMatch(rw_mutex, OS_HandleIsZero())) {
    return;
  }
  OS_LNX_Entity *entity = (OS_LNX_Entity *)rw_mutex.u64[0];
  pthread_rwlock_rdlock(&entity->rwmutex_handle);
}

static void OS_RWMutexDropR(OS_Handle rw_mutex) {
  if (OS_HandleMatch(rw_mutex, OS_HandleIsZero())) {
    return;
  }
  OS_LNX_Entity *entity = (OS_LNX_Entity *)rw_mutex.u64[0];
  pthread_rwlock_unlock(&entity->rwmutex_handle);
}

static void OS_RWMutexTakeW(OS_Handle rw_mutex) {
  if (OS_HandleMatch(rw_mutex, OS_HandleIsZero())) {
    return;
  }
  OS_LNX_Entity *entity = (OS_LNX_Entity *)rw_mutex.u64[0];
  pthread_rwlock_wrlock(&entity->rwmutex_handle);
}

static void OS_RWMutexDropW(OS_Handle rw_mutex) {
  if (OS_HandleMatch(rw_mutex, OS_HandleIsZero())) {
    return;
  }
  OS_LNX_Entity *entity = (OS_LNX_Entity *)rw_mutex.u64[0];
  pthread_rwlock_unlock(&entity->rwmutex_handle);
}

//- rjf: condition variables

static OS_Handle os_condition_variable_alloc(void) {
  OS_LNX_Entity *entity =
      os_lnx_entity_alloc(OS_LNX_EntityKind_ConditionVariable);
  int init_result = pthread_cond_init(&entity->cv.cond_handle, 0);
  if (init_result == -1) {
    os_lnx_entity_release(entity);
    entity = 0;
  }
  int init2_result = 0;
  if (entity) {
    init2_result = pthread_mutex_init(&entity->cv.rwlock_mutex_handle, 0);
  }
  if (init2_result == -1) {
    pthread_cond_destroy(&entity->cv.cond_handle);
    os_lnx_entity_release(entity);
    entity = 0;
  }
  OS_Handle handle = {(U64)entity};
  return handle;
}

static void os_condition_variable_release(OS_Handle cv) {
  if (OS_HandleMatch(cv, OS_HandleIsZero())) {
    return;
  }
  OS_LNX_Entity *entity = (OS_LNX_Entity *)cv.u64[0];
  pthread_cond_destroy(&entity->cv.cond_handle);
  pthread_mutex_destroy(&entity->cv.rwlock_mutex_handle);
  os_lnx_entity_release(entity);
}

static B32 os_condition_variable_wait(OS_Handle cv, OS_Handle mutex,
                                      U64 endt_us) {
  if (OS_HandleMatch(cv, OS_HandleIsZero())) {
    return 0;
  }
  if (OS_HandleMatch(mutex, OS_HandleIsZero())) {
    return 0;
  }
  OS_LNX_Entity *cv_entity = (OS_LNX_Entity *)cv.u64[0];
  OS_LNX_Entity *mutex_entity = (OS_LNX_Entity *)mutex.u64[0];
  struct timespec endt_timespec;
  endt_timespec.tv_sec = endt_us / Million(1);
  endt_timespec.tv_nsec =
      Thousand(1) * (endt_us - (endt_us / Million(1)) * Million(1));
  int wait_result = pthread_cond_timedwait(
      &cv_entity->cv.cond_handle, &mutex_entity->mutex_handle, &endt_timespec);
  B32 result = (wait_result != ETIMEDOUT);
  return result;
}

static B32 os_condition_variable_wait_rw_r(OS_Handle cv, OS_Handle mutex_rw,
                                           U64 endt_us) {
  // TODO(rjf): because pthread does not supply cv/rw natively, I had to hack
  // this together, but this would probably just be a lot better if we just
  // implemented the primitives ourselves with e.g. futexes
  //
  if (OS_HandleMatch(cv, OS_HandleIsZero())) {
    return 0;
  }
  if (OS_HandleMatch(mutex_rw, OS_HandleIsZero())) {
    return 0;
  }
  OS_LNX_Entity *cv_entity = (OS_LNX_Entity *)cv.u64[0];
  OS_LNX_Entity *rw_mutex_entity = (OS_LNX_Entity *)mutex_rw.u64[0];
  struct timespec endt_timespec;
  endt_timespec.tv_sec = endt_us / Million(1);
  endt_timespec.tv_nsec =
      Thousand(1) * (endt_us - (endt_us / Million(1)) * Million(1));
  B32 result = 0;
  for (;;) {
    pthread_mutex_lock(&cv_entity->cv.rwlock_mutex_handle);
    int wait_result = pthread_cond_timedwait(&cv_entity->cv.cond_handle,
                                             &cv_entity->cv.rwlock_mutex_handle,
                                             &endt_timespec);
    if (wait_result != ETIMEDOUT) {
      pthread_rwlock_rdlock(&rw_mutex_entity->rwmutex_handle);
      pthread_mutex_unlock(&cv_entity->cv.rwlock_mutex_handle);
      result = 1;
      break;
    }
    pthread_mutex_unlock(&cv_entity->cv.rwlock_mutex_handle);
    if (wait_result == ETIMEDOUT) {
      break;
    }
  }
  return result;
}

static B32 os_condition_variable_wait_rw_w(OS_Handle cv, OS_Handle mutex_rw,
                                           U64 endt_us) {
  // TODO(rjf): because pthread does not supply cv/rw natively, I had to hack
  // this together, but this would probably just be a lot better if we just
  // implemented the primitives ourselves with e.g. futexes
  //
  if (OS_HandleMatch(cv, OS_HandleIsZero())) {
    return 0;
  }
  if (OS_HandleMatch(mutex_rw, OS_HandleIsZero())) {
    return 0;
  }
  OS_LNX_Entity *cv_entity = (OS_LNX_Entity *)cv.u64[0];
  OS_LNX_Entity *rw_mutex_entity = (OS_LNX_Entity *)mutex_rw.u64[0];
  struct timespec endt_timespec;
  endt_timespec.tv_sec = endt_us / Million(1);
  endt_timespec.tv_nsec =
      Thousand(1) * (endt_us - (endt_us / Million(1)) * Million(1));
  B32 result = 0;
  for (;;) {
    pthread_mutex_lock(&cv_entity->cv.rwlock_mutex_handle);
    int wait_result = pthread_cond_timedwait(&cv_entity->cv.cond_handle,
                                             &cv_entity->cv.rwlock_mutex_handle,
                                             &endt_timespec);
    if (wait_result != ETIMEDOUT) {
      pthread_rwlock_wrlock(&rw_mutex_entity->rwmutex_handle);
      pthread_mutex_unlock(&cv_entity->cv.rwlock_mutex_handle);
      result = 1;
      break;
    }
    pthread_mutex_unlock(&cv_entity->cv.rwlock_mutex_handle);
    if (wait_result == ETIMEDOUT) {
      break;
    }
  }
  return result;
}

static void os_condition_variable_signal(OS_Handle cv) {
  if (OS_HandleMatch(cv, OS_HandleIsZero())) {
    return;
  }
  OS_LNX_Entity *cv_entity = (OS_LNX_Entity *)cv.u64[0];
  pthread_cond_signal(&cv_entity->cv.cond_handle);
}

static void os_condition_variable_broadcast(OS_Handle cv) {
  if (OS_HandleMatch(cv, OS_HandleIsZero())) {
    return;
  }
  OS_LNX_Entity *cv_entity = (OS_LNX_Entity *)cv.u64[0];
  pthread_cond_broadcast(&cv_entity->cv.cond_handle);
}

//- rjf: cross-process semaphores

static OS_Handle OS_SemaphoreAlloc(U32 initial_count, U32 max_count,
                                   String8 name) {
  OS_Handle result = {0};
  if (name.size > 0) {
    // TODO: we need to allocate shared memory to store sem_t
    NotImplemented;
  } else {
    sem_t *s = (sem_t *)mmap(0, sizeof(*s), PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    AssertAlways(s != MAP_FAILED);
    int err = sem_init(s, 0, initial_count);
    if (err == 0) {
      result.u64[0] = (U64)s;
    }
  }
  return result;
}

static void OS_SemaphoreRelease(OS_Handle semaphore) {
  int err = munmap((void *)semaphore.u64[0], sizeof(sem_t));
  AssertAlways(err == 0);
}

static OS_Handle OS_SemaphoreOpen(String8 name) { NotImplemented; }

static void OS_SemaphoreClose(OS_Handle semaphore) { NotImplemented; }

static B32 OS_SemaphoreTake(OS_Handle semaphore, U64 endt_us) {
  AssertAlways(endt_us == max_U64);
  for (;;) {
    int err = sem_wait((sem_t *)semaphore.u64[0]);
    if (err == 0) {
      break;
    } else {
      if (errno == EAGAIN) {
        continue;
      }
    }
    InvalidPath;
    break;
  }
  return 1;
}

static void OS_SemaphoreDrop(OS_Handle semaphore) {
  for (;;) {
    int err = sem_post((sem_t *)semaphore.u64[0]);
    if (err == 0) {
      break;
    } else {
      if (errno == EAGAIN) {
        continue;
      }
    }
    InvalidPath;
    break;
  }
}

////////////////////////////////
//~ rjf: @os_hooks Dynamically-Loaded Libraries (Implemented Per-OS)

static OS_Handle os_library_open(String8 path) {
  Temp scratch = ScratchBegin(0, 0);
  char *path_cstr = (char *)PushStr8Copy(scratch.arena, path).str;
  void *so = dlopen(path_cstr, RTLD_LAZY | RTLD_LOCAL);
  OS_Handle lib = {(U64)so};
  ScratchEnd(scratch);
  return lib;
}

static VoidProc *os_library_load_proc(OS_Handle lib, String8 name) {
  Temp scratch = ScratchBegin(0, 0);
  void *so = (void *)lib.u64;
  char *name_cstr = (char *)PushStr8Copy(scratch.arena, name).str;
  VoidProc *proc = (VoidProc *)dlsym(so, name_cstr);
  ScratchEnd(scratch);
  return proc;
}

static void os_library_close(OS_Handle lib) {
  void *so = (void *)lib.u64;
  dlclose(so);
}

////////////////////////////////
//~ rjf: @os_hooks Safe Calls (Implemented Per-OS)

static void os_safe_call(OS_ThreadFunctionType *func,
                         OS_ThreadFunctionType *fail_handler, void *ptr) {
  // rjf: push handler to chain
  OS_LNX_SafeCallChain chain = {0};
  SLLStackPush(os_lnx_safe_call_chain, &chain);
  chain.fail_handler = fail_handler;
  chain.ptr = ptr;

  // rjf: set up sig handler info
  struct sigaction new_act = {0};
  new_act.sa_handler = os_lnx_safe_call_sig_handler;
  int signals_to_handle[] = {
      SIGILL, SIGFPE, SIGSEGV, SIGBUS, SIGTRAP,
  };
  struct sigaction og_act[ArrayCount(signals_to_handle)] = {0};

  // rjf: attach handler info for all signals
  for (U32 i = 0; i < ArrayCount(signals_to_handle); i += 1) {
    sigaction(signals_to_handle[i], &new_act, &og_act[i]);
  }

  // rjf: call function
  func(ptr);

  // rjf: reset handler info for all signals
  for (U32 i = 0; i < ArrayCount(signals_to_handle); i += 1) {
    sigaction(signals_to_handle[i], &og_act[i], 0);
  }
}

////////////////////////////////
//~ rjf: @os_hooks GUIDs (Implemented Per-OS)

static Guid os_make_guid(void) {
  Guid guid = {0};
  getrandom(guid.v, sizeof(guid.v), 0);
  guid.data3 &= 0x0fff;
  guid.data3 |= (4 << 12);
  guid.data4[0] &= 0x3f;
  guid.data4[0] |= 0x80;
  return guid;
}

////////////////////////////////
//~ mgj: Hot Reload
int CopyFile(const char *src, const char *dst) {
  unlink(dst);
  int in_fd = open(src, O_RDONLY);
  int out_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);

  struct stat st;
  fstat(in_fd, &st);

  off_t bytes = st.st_size;
  sendfile(out_fd, in_fd, NULL, bytes);

  close(in_fd);
  close(out_fd);
  return 0;
}

static int LoadDLL(DllInfo *dll_info, String8 path_to_lib) {
  void *handle = dlopen((char *)path_to_lib.str, RTLD_NOW | RTLD_LOCAL);
  dll_info->dll_handle = handle;
  if (!handle) {
    printf("Failed to load entrypoint.so: %s", dlerror());
    return 0;
  }

  dll_info->func = (OS_Handle (*)(void *))dlsym(handle, dll_info->func_name);
  if (!dll_info->func) {
    printf("Failed to load function: %s due to error: %s", dll_info->func_name,
           dlerror());
    return 0;
  }

  dll_info->cleanup_func =
      (void (*)(void *))dlsym(handle, dll_info->cleanup_func_name);
  if (!dll_info->cleanup_func) {
    printf("Failed to load function: %s due to error: %s",
           dll_info->cleanup_func_name, dlerror());
    return 0;
  }

  char *err = dlerror();
  if (err != NULL) {
    printf("%s", err);
    return 0;
  }

  return 1;
}
static struct stat old_file, new_file;
void OS_HotReload(Context *ctx) {
  ScratchScope scratch = ScratchScope(0, 0);

  const char *entrypoint_func_name = "Entrypoint";
  const char *cleanup_func_name = "Cleanup";

  // TODO: move out of hotpath
  DllInfo *dll_info = ctx->dll_info;
  dll_info->func_name = entrypoint_func_name;
  dll_info->cleanup_func_name = cleanup_func_name;
  dll_info->dll_path = Str8PathFromStr8List(
      scratch.arena, {ctx->cwd, Str8CString("build"), Str8CString("linux"),
                      Str8CString("debug"), Str8CString("entrypoint.so")});
  dll_info->dll_temp_path = Str8PathFromStr8List(
      scratch.arena, {ctx->cwd, Str8CString("build"), Str8CString("linux"),
                      Str8CString("debug"), Str8CString("entrypoint_temp.so")});

  if (ctx->running) {
    B32 file_info_is_not_available =
        (stat((char *)dll_info->dll_temp_path.str, &new_file) != 0);
    if (file_info_is_not_available) {
      printf("Hot Reloading failed due to error when receiving file "
             "information: %s\n",
             strerror(errno));
    } else if (((old_file.st_size != new_file.st_size) ||
                (old_file.st_mtime != new_file.st_mtime))) {
      dll_info->cleanup_func(ctx);

      if (dlclose(dll_info->dll_handle)) {
        printf("Failed to close entrypoint.so: %s", dlerror());
        exit(EXIT_FAILURE);
      };
      dll_info->dll_handle = 0;

      if (stat((char *)dll_info->dll_temp_path.str, &old_file) != 0) {
        printf("Hot Reload Failed, because stat failed: %s", strerror(errno));
        return;
      }

      if (CopyFile((char *)dll_info->dll_temp_path.str,
                   (char *)dll_info->dll_path.str) != 0) {
        printf("Hot Reload Failed, because renaming failed: %s",
               strerror(errno));
        return;
      }

      if (LoadDLL(dll_info, dll_info->dll_path)) {
        ctx->running = 1;
        ctx->os_state = &os_lnx_state;

        dll_info->entrypoint_thread_handle = dll_info->func((void *)ctx);
      } else {
        printf("Failed to reload DLL\n");
        Trap();
      }
    }
  } else if (!ctx->running) {
    if (stat((char *)dll_info->dll_temp_path.str, &old_file) != 0) {
      printf("Hot Reload Failed, because stat failed: %s", strerror(errno));
      return;
    }

    if (CopyFile((char *)dll_info->dll_temp_path.str,
                 (char *)dll_info->dll_path.str) != 0) {
      printf("Hot Reload Failed, because renaming failed: %s\n",
             strerror(errno));
      return;
    }

    if (LoadDLL(dll_info, dll_info->dll_path)) {
      ctx->running = 1;
      ctx->os_state = &os_lnx_state;

      dll_info->entrypoint_thread_handle = dll_info->func((void *)ctx);
    } else {
      printf("Failed to reload DLL\n");
      Trap();
    }
  }
}

static void OS_GlobalStateSetFromPtr(void *ptr) {
  os_lnx_state = *(OS_LNX_State *)ptr;
}

////////////////////////////////
//~ rjf: @os_hooks Entry Points (Implemented Per-OS)

int main(int argc, char **argv) {
  //- rjf: set up OS layer
  {
    //- rjf: get statically-allocated system/process info
    {
      OS_SystemInfo *info = &os_lnx_state.system_info;
      info->logical_processor_count = (U32)get_nprocs();
      info->page_size = (U64)getpagesize();
      info->large_page_size = MB(2);
      info->allocation_granularity = info->page_size;
    }
    {
      OS_ProcessInfo *info = &os_lnx_state.process_info;
      info->pid = (U32)getpid();
    }

    //- rjf: set up thread context
    local_persist TCTX tctx;
    tctx_init_and_equip(&tctx);

    //- rjf: set up dynamically allocated state
    os_lnx_state.arena = ArenaAlloc();
    os_lnx_state.entity_arena = ArenaAlloc();
    pthread_mutex_init(&os_lnx_state.entity_mutex, 0);

    //- rjf: grab dynamically allocated system info
    {
      Temp scratch = ScratchBegin(0, 0);
      OS_SystemInfo *info = &os_lnx_state.system_info;

      // rjf: get machine name
      B32 got_final_result = 0;
      U8 *buffer = 0;
      int size = 0;
      for (S64 cap = 4096, r = 0; r < 4; cap *= 2, r += 1) {
        ScratchEnd(scratch);
        buffer = PushArrayNoZero(scratch.arena, U8, cap);
        size = gethostname((char *)buffer, cap);
        if (size < cap) {
          got_final_result = 1;
          break;
        }
      }

      // rjf: save name to info
      if (got_final_result && size > 0) {
        info->machine_name.size = size;
        info->machine_name.str = PushArrayNoZero(os_lnx_state.arena, U8,
                                                 info->machine_name.size + 1);
        MemoryCopy(info->machine_name.str, buffer, info->machine_name.size);
        info->machine_name.str[info->machine_name.size] = 0;
      }

      ScratchEnd(scratch);
    }

    //- rjf: grab dynamically allocated process info
    {
      Temp scratch = ScratchBegin(0, 0);
      OS_ProcessInfo *info = &os_lnx_state.process_info;

      // rjf: grab binary path
      {
        // rjf: get self string
        B32 got_final_result = 0;
        U8 *buffer = 0;
        int size = 0;
        for (S64 cap = PATH_MAX, r = 0; r < 4; cap *= 2, r += 1) {
          ScratchEnd(scratch);
          buffer = PushArrayNoZero(scratch.arena, U8, cap);
          size = readlink("/proc/self/exe", (char *)buffer, cap);
          if (size < cap) {
            got_final_result = 1;
            break;
          }
        }

        // rjf: save
        if (got_final_result && size > 0) {
          String8 full_name = Str8(buffer, size);
          String8 name_chopped = str8_chop_last_slash(full_name);
          info->binary_path = PushStr8Copy(os_lnx_state.arena, name_chopped);
        }
      }

      // rjf: grab initial directory
      {
        info->initial_path = OS_GetCurrentPath(os_lnx_state.arena);
      }

      // rjf: grab home directory
      {
        char *home = getenv("HOME");
        info->user_program_data_path = Str8CString(home);
      }

      ScratchEnd(scratch);
    }
  }

  //- rjf: call into "real" entry point
  App(OS_HotReload);
}
