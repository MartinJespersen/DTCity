#pragma once

struct SIM_AgentSnapshot
{
    U64 timestamp;
    String8 id;
    F64 x;
    F64 y;
};

struct SIM_AgentSnapshotChunk
{
    SIM_AgentSnapshotChunk* next;
    SIM_AgentSnapshot snapshots[10];
    U32 count;
};

struct SIM_AgentSnapshotChunkList
{
    SIM_AgentSnapshotChunk* first;
    SIM_AgentSnapshotChunk* last;
    U64 total_snapshots;
};

struct SIM_AgentSnapshotNode
{
    SIM_AgentSnapshotNode* next;
    SIM_AgentSnapshot snapshot;
    U64 id;
};

struct SIM_AgentSnapshotList
{
    SIM_AgentSnapshotNode* first;
    SIM_AgentSnapshotNode* last;
};

struct SIM_AgentSnapshotBucket
{
    Arena* arena;
    SIM_AgentSnapshot* snapshots;
    U64 total_snapshots;
    U32 read_to;
};

struct SIM_ParseErrorNode
{
    SIM_ParseErrorNode* next;
    String8 message;
};

struct SIM_ParseErrorList
{
    SIM_ParseErrorNode* first;
    SIM_ParseErrorNode* last;
    U32 error_count;
};
struct SIM_ParseResult
{
    SIM_AgentSnapshotChunkList agent_snapshot_list;

    SIM_ParseErrorList errors;
    B32 success;
};

struct SIM_UpdateResult
{
    U64 timestamp;
    U64 total_snapshots;
    SIM_AgentSnapshot* snapshots;
};

struct SIM_AgentLocation
{
    String8 str_id;
    Vec2F64 pos;
};

struct SIM_AgentLocationChunk
{
    SIM_AgentLocationChunk* next;
    SIM_AgentLocation* locations;
    U32 cap;
    U32 count;
};

struct SIM_AgentLocationChunkList
{
    SIM_AgentLocationChunk* first;
    SIM_AgentLocationChunk* last;
    U32 chunk_count;
    U64 total_locations;
};

struct SIM_AgentLocationResult
{
    SIM_AgentLocation* agent_locations;
    U64 total_locations;
};

struct SIM_Ctx
{
    Arena* arena;
    SIM_AgentSnapshotBucket bucket;
    B32 file_loaded;
    F64 sim_start_sec;
    U64 sim_start_timestamp;
    U64 read_to_timestamp;
    U64 snapshot_freq;

    // hashmap for latest agent snapshots
    SIM_AgentSnapshotList* hashmap;
    U64 hashmap_size;
};

//~mgj: Globals
static SIM_Ctx* g_sim_ctx;

static void
SIM_Init();
static void
SIM_Del();

static void
SIM_Start(F64 sim_start_sec, U64 sim_snapshot_freq_sec = 1);

static SIM_UpdateResult
SIM_Update(Arena* arena, F64 cur_time_sec);

static void
SIM_Read(String8 file_path);
static void
SIM_AgentSnapshotChunkAdd(Arena* arena, SIM_AgentSnapshotChunkList* list,
                          SIM_AgentSnapshot* snapshot);
static void
SIM_AgentLocationChunkAdd(Arena* arena, SIM_AgentLocationChunkList* list, U32 cap,
                          SIM_AgentLocation* agent_loc);
// ~mgj: Implemented by the simulator used
static SIM_ParseResult
SIM_Parse(Arena* arena, String8 text);
