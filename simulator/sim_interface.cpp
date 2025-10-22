static void
SIM_Init()
{
    // permanent arena
    Arena* arena = ArenaAlloc();
    g_sim_ctx->arena = arena;

    // bucket
    Arena* bucket_arena = ArenaAlloc();
    g_sim_ctx = PushStruct(bucket_arena, SIM_Ctx);
    g_sim_ctx->bucket.arena = bucket_arena;
}

static void
SIM_Del()
{
    ArenaRelease(g_sim_ctx->bucket.arena);
    ArenaRelease(g_sim_ctx->arena);
}

static void
SIM_Start(F64 sim_start_sec, U64 sim_snapshot_freq_sec)
{
    g_sim_ctx->sim_start_sec = sim_start_sec;
    g_sim_ctx->snapshot_freq = sim_snapshot_freq_sec;

    // Create a persistent hash map of car ids and locations for quick lookup
    //  - Only store the strings of car ids
    //  - two snapshots should be stored for the current and previous timestep for agent to allow
    //    quick lookup to allow interpolation of position
    // if (car not in car_map)
    // then: add car and use the next next update location as the current and store the current
    // agent with timestamp and location
    // else: update the current and previous locations of the car
    // find the distance between the car and the target node
}

static SIM_UpdateResult
SIM_Update(Arena* arena, F64 cur_time_sec)
{
    F64 sec_since_start = cur_time_sec - g_sim_ctx->sim_start_sec;
    if (g_sim_ctx->file_loaded == false)
    {
        SIM_Read(S("C:\\matsim\\scenarios\\locations.csv"));
        if (g_sim_ctx->bucket.total_snapshots > 0)
        {
            g_sim_ctx->sim_start_timestamp = g_sim_ctx->bucket.snapshots[0].timestamp;
            g_sim_ctx->read_to_timestamp = g_sim_ctx->sim_start_timestamp + 1;
        }
        g_sim_ctx->file_loaded = true;
    }

    F64 cur_sim_sec = (F64)g_sim_ctx->sim_start_timestamp + sec_since_start;

    if (cur_sim_sec > (F64)g_sim_ctx->read_to_timestamp)
    {
        g_sim_ctx->read_to_timestamp += g_sim_ctx->snapshot_freq;
    }

    // read from bucket
    U64 buffer_index = 0;
    SIM_AgentSnapshotBucket* bucket = &g_sim_ctx->bucket;
    SIM_AgentLocationChunkList chunk_list = {};
    for (buffer_index = bucket->read_to; buffer_index < bucket->total_snapshots; buffer_index++)
    {
        if (bucket->snapshots[buffer_index].timestamp > g_sim_ctx->read_to_timestamp)
        {
            break;
        }
        SIM_AgentSnapshot* new_snapshot = &bucket->snapshots[buffer_index];

        // hashmap lookup
        U64 id = HashU128FromStr8(new_snapshot->id).u64[1];
        U32 hashmap_idx = id % g_sim_ctx->hashmap_size;
        SIM_AgentSnapshotList* agent_snapshot_list = &g_sim_ctx->hashmap[hashmap_idx];
        SIM_AgentSnapshotNode* snapshot_node = agent_snapshot_list->first;
        for (; snapshot_node != NULL; snapshot_node = snapshot_node->next)
        {
            if (snapshot_node->id == id)
            {
                break;
            }
        }

        if (snapshot_node == 0)
        {
            snapshot_node = PushStruct(arena, SIM_AgentSnapshotNode);
            SLLQueuePush(agent_snapshot_list->first, agent_snapshot_list->last, snapshot_node);
            snapshot_node->id = id;
            snapshot_node->snapshot.id = PushStr8Copy(g_sim_ctx->arena, new_snapshot->id);
        }

        // ~mgj: New snapshot needs to be used as the reference timestamp has increased
        // Snapshots that are less than the current read_to_timestamp is inserted into cache for
        // location interpolation
        if (bucket->snapshots[buffer_index].timestamp < g_sim_ctx->read_to_timestamp)
        {
            bucket->read_to += 1;

            snapshot_node->snapshot.timestamp = new_snapshot->timestamp;
            snapshot_node->snapshot.x = new_snapshot->x;
            snapshot_node->snapshot.y = new_snapshot->y;
        }

        SIM_AgentLocation agent_loc = {};
        agent_loc.str_id = PushStr8Copy(arena, new_snapshot->id);
        agent_loc.pos = {new_snapshot->x, new_snapshot->y};

        // ~mgj: interpolate position between cached and new snapshot
        if (new_snapshot->timestamp > snapshot_node->snapshot.timestamp)
        {
            U64 timestamp_diff = new_snapshot->timestamp - snapshot_node->snapshot.timestamp;
            F64 time_diff_norm = cur_time_sec / (F64)timestamp_diff;

            Vec2F64 old_pos = {snapshot_node->snapshot.x, snapshot_node->snapshot.y};
            agent_loc.pos = old_pos + (agent_loc.pos - old_pos) * time_diff_norm;
        }

        SIM_AgentLocationChunkAdd(arena, &chunk_list, 64, &agent_loc);
        // the should be a snapshot saved between the cur_time to interpolate between
    }

    // Prepare output
    // result.timestamp = next_sim_timestamp;
    // result.total_snapshots = buffer_index;
    // result.snapshots = PushArray(arena, SIM_AgentSnapshot, buffer_index);
    // MemoryCopy(bucket->snapshots + g_sim_ctx->buf_progress_idx, bucket->snapshots,
    //            buffer_index * sizeof(SIM_AgentSnapshot));

    // g_sim_ctx->buf_progress_idx += buffer_index;
    return result;
}

static void
SIM_AgentLocationChunkAdd(Arena* arena, SIM_AgentLocationChunkList* list, U32 cap,
                          SIM_AgentLocation* agent_loc)
{
    SIM_AgentLocationChunk* chunk = list->last;
    if (chunk || chunk->count >= chunk->cap)
    {
        chunk = PushStruct(arena, SIM_AgentLocationChunk);
        SLLQueuePush(list->first, list->last, chunk);
        chunk->cap = cap;
        chunk->locations = PushArray(arena, SIM_AgentLocation, cap);
        list->chunk_count += 1;
    }

    chunk->locations[chunk->count++] = *agent_loc;
    list->total_locations += 1;
}

static void
SIM_Read(String8 file_path)
{
    ScratchScope scratch = ScratchScope(0, 0);

    OS_Handle file = OS_FileOpen(OS_AccessFlag_Read | OS_AccessFlag_ShareRead, file_path);
    FileProperties file_props = OS_PropertiesFromFile(file);
    U64 file_size = file_props.size;

    Rng1U64 read_rng = {0, file_size};
    U8* buf_start = PushArray(scratch.arena, U8, file_size + 1); // extra byte for null terminator
    U64 bytes_read = OS_FileRead(file, read_rng, buf_start);
    OS_FileClose(file);

    String8 text = {.str = buf_start, .size = file_size};
    Assert(bytes_read == file_size);

    // ~mgj: Parse the text
    SIM_ParseResult parsed_result = SIM_Parse(g_sim_ctx->bucket.arena, text);

    // ~mgj: Add the parsed result to bucket
    SIM_AgentSnapshotBucket* bucket = &g_sim_ctx->bucket;
    ArenaClear(bucket->arena);
    SIM_AgentSnapshotChunkList* list = &parsed_result.agent_snapshot_list;
    {
        bucket->total_snapshots = list->total_snapshots;
        bucket->snapshots =
            PushArrayNoZero(bucket->arena, SIM_AgentSnapshot, list->total_snapshots);
        U64 offset = 0;
        for (SIM_AgentSnapshotChunk* node = list->first; node != 0; node = node->next)
        {
            U64 node_byte_count = sizeof(SIM_AgentSnapshot) * node->count;
            MemoryCopy(bucket->snapshots + offset, node->snapshots, node_byte_count);
            offset += node->count;
        }
    }

    if (bucket->total_snapshots > 0)
    {
        g_sim_ctx->sim_start_timestamp = bucket->snapshots[0].timestamp;
    }
}

static void
SIM_AgentSnapshotChunkAdd(Arena* arena, SIM_AgentSnapshotChunkList* list,
                          SIM_AgentSnapshot* snapshot)
{
    SIM_AgentSnapshotChunk* cur_node = list->last;
    if (list->last == 0 || cur_node->count >= ArrayCount(cur_node->snapshots))
    {
        cur_node = PushStruct(arena, SIM_AgentSnapshotChunk);
        SLLQueuePush(list->first, list->last, cur_node);
    }

    cur_node->snapshots[cur_node->count++] = *snapshot;
    list->total_snapshots += 1;
}
