g_internal U64
_hash_u64_from_str8(String8 str)
{
    return hash_u128_from_str8(str).u64[1];
}

g_internal B32
cache_needs_update(String8 cache_data_file, String8 cache_meta_file)
{
    ScratchScope scratch = ScratchScope(0, 0);
    U64 file_hash = 0;
    U64 file_timestamp = 0;
    B32 update_needed = false;

    U64 meta_file_size = 0;
    OS_Handle meta_data_file_handle = os_file_open(OS_AccessFlag_Read, cache_meta_file);
    FileProperties meta_file_props = os_properties_from_file(meta_data_file_handle);
    meta_file_size = meta_file_props.size;
    if (meta_file_size == 0)
    {
        update_needed = true;
    }

    if (update_needed == false)
    {
        Rng1U64 meta_read_range = {0, meta_file_size};
        String8 meta_data_str = push_str8_fill_byte(scratch.arena, meta_file_size, 0);
        U64 meta_file_bytes_read = os_file_read(meta_data_file_handle, meta_read_range, meta_data_str.str);

        if (meta_file_bytes_read != meta_file_size)
        {
            DEBUG_LOG("DataFetch: Could not read all data from meta cache file: %s", cache_meta_file.str);
            update_needed = true;
        }
        U8* cur_byte = meta_data_str.str;
        if (update_needed == false)
        {
            U64 cur_file_hash = _hash_u64_from_str8(cache_data_file);

            //~mgj: read hash and ttl from file
            while (*cur_byte != 0 && *cur_byte != '\t')
            {
                cur_byte += 1;
            }
            Rng1U64 read_range = {0, U64(cur_byte - meta_data_str.str)};
            String8 u64_str = Str8Substr(meta_data_str, read_range);
            file_hash = U64FromStr8(u64_str, 10);
            if (cur_file_hash != file_hash)
            {
                update_needed = true;
            }

            if (update_needed == false)
            {
                while (*cur_byte == '\t')
                {
                    cur_byte += 1;
                }

                U64 cur_time = os_now_unix();
                U8* ttl_base = cur_byte;
                while (*cur_byte != 0)
                {
                    cur_byte += 1;
                }

                Rng1U64 ttl_range = {U64(ttl_base - meta_data_str.str), U64(cur_byte - meta_data_str.str)};
                file_timestamp = U64FromStr8(Str8Substr(meta_data_str, ttl_range), 10);
                if (cur_time > file_timestamp + 3600) // hard coded ttl of 1 hour
                {
                    update_needed = true;
                }
            }
        }
        os_file_close(meta_data_file_handle);
    }

    return update_needed;
}

g_internal void
cache_write(String8 cache_file, String8 content, String8 hash_content)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(0, 0);

    // create name of meta file
    String8 cache_meta_file = str8_concat(scratch.arena, cache_file, S(".meta"));

    // ~mgj: write to cache file
    OS_Handle file_write_handle = os_file_open(OS_AccessFlag_Write, cache_file);
    U64 bytes_written = OS_FileWrite(file_write_handle, r1u64(0, content.size), content.str);
    if (bytes_written != content.size)
    {
        DEBUG_LOG("DataFetch: Was not able to write OSM data to cache\n");
    }
    os_file_close(file_write_handle);

    // ~mgj: write to cache meta file
    if (content.size > 0)
    {
        U64 new_hash = _hash_u64_from_str8(hash_content);
        U64 timestamp = os_now_unix();
        String8 meta_str = PushStr8F(scratch.arena, "%llu\t%llu", new_hash, timestamp);

        OS_Handle file_write_handle_meta = os_file_open(OS_AccessFlag_Write, cache_meta_file);
        U64 bytes_written_meta = OS_FileWrite(file_write_handle_meta, r1u64(0, meta_str.size), meta_str.str);
        if (bytes_written_meta != meta_str.size)
        {
            DEBUG_LOG("DataFetch: Was not able to write meta data to cache\n");
        }
        os_file_close(file_write_handle_meta);
    }
}

g_internal Result<String8>
cache_read(Arena* arena, String8 cache_file, String8 hash_input)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(&arena, 1);

    String8 cache_meta_file = str8_concat(scratch.arena, cache_file, S(".meta"));

    B32 read_from_cache = false;
    String8 str = {};

    if (os_file_path_exists(cache_file))
    {
        read_from_cache = true;
        if (os_file_path_exists(cache_meta_file) == false)
        {
            read_from_cache = false;
        }
    }

    if (read_from_cache)
    {
        OS_Handle file_handle = os_file_open(OS_AccessFlag_Read, cache_file);
        FileProperties file_props = os_properties_from_file(file_handle);

        str = push_str8_fill_byte(arena, file_props.size, 0);
        U64 total_read_size = os_file_read(file_handle, r1u64(0, file_props.size), str.str);

        if (total_read_size != file_props.size)
        {
            DEBUG_LOG("DataFetch: Could not read everything from cache\n");
        }
        os_file_close(file_handle);

        B32 needs_update = cache_needs_update(hash_input, cache_meta_file);
        read_from_cache = !needs_update;
    }

    Result<String8> res = {str, !read_from_cache};
    return res;
}
