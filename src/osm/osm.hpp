#pragma once

#include "base/base_inc.hpp"
#include <memory>

namespace async
{
struct ThreadPool;
struct ThreadInfo;
template <typename T>
struct UserFuncResult;
template <typename T>
struct AsyncTaskContinuation;
template <typename T>
struct AsyncTaskStatus;
} // namespace async

namespace neta
{
struct EdgeList;
}
namespace city
{
struct City;
};

namespace osm
{

typedef S64 EdgeId;
typedef U64 NodeId;
typedef S64 WayId;

struct WgsNode
{
    WgsNode* next;
    NodeId id;
    F32 lat;
    F32 lon;
};

struct Tag
{
    Tag* next;
    String8 key;
    String8 value;
};

enum class TagResultEnum : int
{
    Found = 0,
    NotFound = 1,
};

struct TagResult
{
    TagResultEnum result;
    String8 value;
};

struct WgsNodeList
{
    WgsNode* first;
    WgsNode* last;
};

struct NodeParseResult
{
    Buffer<WgsNodeList> node_hashmap;
    B32 error;
};

struct Way
{
    Way* next;

    WayId id;

    NodeId* node_ids; // fixed array with node_count length and node ids as index
    U64 node_count;

    Buffer<Tag> tags;
};

struct WayNode
{
    WayNode* next;
    WayNode* hash_next;
    Way way;
};

struct WayList
{
    WayNode* first;
    WayNode* last;
};

struct WayParseResult
{
    Buffer<Way> ways;
    B32 error;
};

struct Node
{
    Node* next;
    NodeId id;

    String8 utm_zone;  // TODO: If not used in future: Delete
    WayList way_queue; // Linked list of ways sharing this node
};

struct EcefLocation
{
    NodeId id;
    Vec3F64 pos;
};

struct WgsLocation
{
    NodeId id;
    F64 lat;
    F64 lon;
};

// Segment between consecutive nodes in an OSM way. Currently built for highways.
struct Edge
{
    Edge* prev;
    Edge* next;
    S64 id;
    S64 node_id_from;
    S64 node_id_to;
    S64 way_id;
};

struct EdgeStructure
{
    Buffer<Edge> edges;
    Map<EdgeId, Edge*> edge_map;
};

struct NodeList
{
    Node* first;
    Node* last;
};

#define WAYTYPE_OPTIONS     \
    X(Building, "building") \
    X(Highway, "highway")

enum class WayType : U32
{
#define X(name, str) name,
    WAYTYPE_OPTIONS
#undef X
        Count
};

read_only g_internal const char* g_waytype_osm_tag[] = {
#define X(name, str) str,
    WAYTYPE_OPTIONS
#undef X
};

struct NetworkCache
{
    String8 file_location;
    String8 bbox_str;
};

struct Network
{
    Allocator allocator;

    Buffer<NodeList> node_hashmap; // key is the node id
    Map<NodeId, EcefLocation>* ecef_location_map;
    Map<NodeId, WgsLocation>* wgs_location_map;

    Buffer<WayList> way_hashmap;                    // view into way buffers
    Buffer<Way> ways_arr[enum_idx(WayType::Count)] = {}; // buffer storage
    Buffer<NodeId> node_id_arr[enum_idx(WayType::Count)] = {};
    EdgeStructure edge_structure;

    Network(U64 node_hashmap_size, U64 way_hashmap_size) noexcept;
};

// Owned by the task allocator; the result is transferred only after completion.
struct NetworkLoadTask
{
    NetworkCache cache;
    String8 request_body;
    U64 node_hashmap_size;
    U64 way_hashmap_size;
    std::unique_ptr<Network> result;
};

async::AsyncTaskStatus<NetworkLoadTask>*
network_load(async::ThreadPool* thread_pool, NetworkCache cache, String8 request_body,
             U64 node_hashmap_size = 1000, U64 way_hashmap_size = 100);


// Public
EcefLocation
ecef_location_create(U64 id, Vec3F64 pos);

TagResult
tag_find(Arena* arena, Buffer<Tag> tags, String8 tag_to_find);
WayNode*
way_find(Network* network, WayId way_id);
EcefLocation
location_get(Network* network, U64 node_id);
WgsLocation
wgs_location_get(Network* network, U64 node_id);

// Private task stages share task-owned inputs and publish only a complete result.
g_internal async::AsyncTaskContinuation<NetworkLoadTask>
_network_load_begin(async::ThreadInfo thread_info, async::AsyncTaskStatus<NetworkLoadTask>* task);
g_internal async::UserFuncResult<NetworkLoadTask>
_network_download_complete(Arena* arena, async::ThreadPool* thread_pool, String8 response_body, NetworkLoadTask* input);
g_internal Error
_network_parse(NetworkLoadTask* input, String8 file_content);
g_internal Error
_parse_osm_data(Network* network, String8 file_content);
g_internal void
_highway_edges_create(Network* network);
g_internal WgsNode*
_wgs_node_find(Buffer<WgsNodeList> node_hashmap, U64 node_id);
g_internal B32
_node_hashmap_insert(Network* network, U64 node_id, Way* way, Node** out);
} // namespace osm
