// Copyright (c) 2024 Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Handle Type Functions (Helpers, Implemented Once)

#include "os_core/os_core.hpp"
lib_internal OS_Handle
OS_HandleIsZero()
{
    OS_Handle handle = {0};
    return handle;
}

lib_internal OS_Handle
OS_HandleFromPtr(void* ptr)
{
    OS_Handle handle = {0};
    handle.u64[0] = (U64)ptr;
    return handle;
}

lib_internal B32
OS_HandleMatch(OS_Handle a, OS_Handle b)
{
    return a.u64[0] == b.u64[0];
}

lib_internal void
os_handle_list_push(Arena* arena, OS_HandleList* handles, OS_Handle handle)
{
    OS_HandleNode* n = PushArray(arena, OS_HandleNode, 1);
    n->v = handle;
    SLLQueuePush(handles->first, handles->last, n);
    handles->count += 1;
}

lib_internal OS_HandleArray
os_handle_array_from_list(Arena* arena, OS_HandleList* list)
{
    OS_HandleArray result = {0};
    result.count = list->count;
    result.v = PushArrayNoZero(arena, OS_Handle, result.count);
    U64 idx = 0;
    for (OS_HandleNode* n = list->first; n != 0; n = n->next, idx += 1)
    {
        result.v[idx] = n->v;
    }
    return result;
}

////////////////////////////////
//~ rjf: Command Line Argc/Argv Helper (Helper, Implemented Once)

lib_internal String8List
os_string_list_from_argcv(Arena* arena, int argc, char** argv)
{
    String8List result = {0};
    for (int i = 0; i < argc; i += 1)
    {
        String8 str = str8_c_string(argv[i]);
        str8_list_push(arena, &result, str);
    }
    return result;
}

////////////////////////////////
//~ rjf: Filesystem Helpers (Helpers, Implemented Once)

lib_internal String8
os_data_from_file_path(Arena* arena, String8 path)
{
    OS_Handle file = os_file_open(OS_AccessFlag_Read | OS_AccessFlag_ShareRead, path);
    FileProperties props = os_properties_from_file(file);
    String8 data = os_string_from_file_range(arena, file, r1u64(0, props.size));
    os_file_close(file);
    return data;
}

lib_internal B32
os_write_data_to_file_path(String8 path, String8 data)
{
    B32 good = 0;
    OS_Handle file = os_file_open(OS_AccessFlag_Write, path);
    if (!OS_HandleMatch(file, OS_HandleIsZero()))
    {
        good = 1;
        os_file_write(file, r1u64(0, data.size), data.str);
        os_file_close(file);
    }
    return good;
}

lib_internal B32
os_write_data_list_to_file_path(String8 path, String8List list)
{
    B32 good = 0;
    OS_Handle file = os_file_open(OS_AccessFlag_Write, path);
    if (!OS_HandleMatch(file, OS_HandleIsZero()))
    {
        good = 1;
        U64 off = 0;
        for (String8Node* n = list.first; n != 0; n = n->next)
        {
            os_file_write(file, r1u64(off, off + n->string.size), n->string.str);
            off += n->string.size;
        }
        os_file_close(file);
    }
    return good;
}

lib_internal B32
os_make_parent_directory_if_missing(String8 file_path)
{
    String8 dir = str8_chop_last_slash(file_path);
    B32 good = 1;
    if (dir.size > 0 && !os_folder_path_exists(dir))
    {
        good = os_make_directory(dir);
    }
    return good;
}

lib_internal B32
os_append_data_to_file_path(String8 path, String8 data, OS_AccessFlags additional_flags)
{
    B32 good = 0;
    if (data.size != 0)
    {
        OS_Handle file = os_file_open(OS_AccessFlag_Write | OS_AccessFlag_Append | additional_flags, path);
        if (!OS_HandleMatch(file, OS_HandleIsZero()))
        {
            good = 1;
            U64 pos = os_properties_from_file(file).size;
            os_file_write(file, r1u64(pos, pos + data.size), data.str);
            os_file_close(file);
        }
    }
    return good;
}

lib_internal B32
os_clear_directory(String8 path)
{
    if (!os_folder_path_exists(path))
    {
        return 0;
    }

    B32 good = 1;
    Temp scratch = ScratchBegin(0, 0);
    OS_FileIter* iter = os_file_iter_begin(scratch.arena, path, 0);
    OS_FileInfo info = {0};
    while (os_file_iter_next(scratch.arena, iter, &info))
    {
        String8 child_path = str8_path_from_str8_list(scratch.arena, {path, info.name});
        B32 is_folder = !!(info.props.flags & FilePropertyFlag_IsFolder);
        B32 is_link = !!(info.props.flags & FilePropertyFlag_IsLink);
        if (is_folder && !is_link)
        {
            good = os_clear_directory(child_path) && good;
            good = os_delete_directory_at_path(child_path) && good;
        }
        else if (is_folder)
        {
            good = os_delete_directory_at_path(child_path) && good;
        }
        else
        {
            good = os_delete_file_at_path(child_path) && good;
        }
    }
    os_file_iter_end(iter);
    ScratchEnd(scratch);
    return good;
}

lib_internal OS_FileID
os_id_from_file_path(String8 path)
{
    OS_Handle file = os_file_open(OS_AccessFlag_Read | OS_AccessFlag_ShareRead, path);
    OS_FileID id = os_id_from_file(file);
    os_file_close(file);
    return id;
}

lib_internal S64
os_file_id_compare(OS_FileID a, OS_FileID b)
{
    S64 cmp = MemoryCompare((void*)&a.v[0], (void*)&b.v[0], sizeof(a.v));
    return cmp;
}

lib_internal String8
os_string_from_file_range(Arena* arena, OS_Handle file, Rng1U64 range)
{
    U64 pre_pos = arena_pos(arena);
    String8 result;
    result.size = dim_1u64(range);
    result.str = PushArrayNoZero(arena, U8, result.size);
    U64 actual_read_size = os_file_read(file, range, result.str);
    if (actual_read_size < result.size)
    {
        arena_pop_to(arena, pre_pos + actual_read_size);
        result.size = actual_read_size;
    }
    return result;
}

// ~mgj: Timers
force_inline lib_internal U64
os_cpu_timer_read()
{
    return __rdtsc();
}

// ~mgj: Cmdline

lib_internal String8List
os_parse_cmd_line(Arena* arena, int argc, char** argv)
{
    String8List str_list = {};
    for (int i = 0; i < argc; ++i)
    {
        String8 arg = str8_c_string(argv[i]);
        str8_list_push(arena, &str_list, arg);
    }
    return str_list;
}

lib_internal String8
os_arg_from_cmdline(Arena* arena, String8List* list, String8 arg_name)
{
    String8 result = {};
    for (String8Node* n = list->first; n; n = n->next)
    {
        if (str8_match(arg_name, n->string, MatchFlag_CaseInsensitive | MatchFlag_RightSideSloppy))
        {
            U64 split_pos = str8_substr_find(n->string, str8_lit("="), 0, 0);
            String8 value = split_pos < n->string.size ? str8_skip(n->string, split_pos + 1) : Str8Zero();
            result = push_str8_copy(arena, value);
            break;
        }
    }
    return result;
}
