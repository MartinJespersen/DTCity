#include "core_inc.hpp"
#include "base/base_random.hpp"
#include "base/cache.hpp"
#include "async/thread_pool.hpp"
#include "async/async_inc.hpp"
#include "http/http.h"
#include "async/async_http.hpp"
#include "osm/osm.hpp"
#include "lib_wrappers/lib_wrappers_inc.hpp"

namespace osm
{
EcefLocation
ecef_location_create(U64 id, Vec3F64 pos)
{
    EcefLocation loc = {.id = id, .pos = pos};
    return loc;
}

Network::Network(U64 node_hashmap_size, U64 way_hashmap_size) noexcept
    // Allocator disables chaining. Reserve address space for city-sized networks;
    // pages are committed only as parsing needs them.
    : allocator(Allocator::create({.reserve_size = GB(4), .commit_size = arena_default_commit_size,
                                   .flags = arena_default_flags})),
      edge_structure{.edges = {}, .edge_map = Map<EdgeId, Edge*>(&this->allocator, 1024)}
{
    Debug_SetName(this->allocator.arena, "OSM network arena");
    this->node_hashmap = buffer_alloc<NodeList>(this->allocator.arena, node_hashmap_size);
    this->way_hashmap = buffer_alloc<WayList>(this->allocator.arena, way_hashmap_size);
    this->ecef_location_map = Map<NodeId, EcefLocation>::create(this->allocator.arena, node_hashmap_size);
    this->wgs_location_map = Map<NodeId, WgsLocation>::create(this->allocator.arena, node_hashmap_size);
}

async::AsyncTaskStatus<NetworkLoadTask>*
network_load(async::ThreadPool* thread_pool, NetworkCache cache, String8 request_body,
             U64 node_hashmap_size, U64 way_hashmap_size)
{
    Allocator allocator = Allocator::create();
    Debug_SetName(allocator.arena, "OSM load task");
    NetworkLoadTask* input = allocator.make<NetworkLoadTask>();
    input->cache.file_location = push_str8_copy(allocator.arena, cache.file_location);
    input->cache.bbox_str = push_str8_copy(allocator.arena, cache.bbox_str);
    input->request_body = push_str8_copy(allocator.arena, request_body);
    input->node_hashmap_size = node_hashmap_size;
    input->way_hashmap_size = way_hashmap_size;
    return async::async_task_run(std::move(allocator), thread_pool, _network_load_begin, input, "OSM load task");
}

WayNode*
way_find(Network* network, WayId way_id)
{
    WayList* way_list = &network->way_hashmap.data[way_id % network->way_hashmap.size];

    WayNode* node = way_list->first;
    for (; node; node = node->hash_next)
    {
        Way* way = &node->way;
        if (way->id == way_id)
        {
            break;
        }
    }
    return node;
}

TagResult
tag_find(Arena* arena, Buffer<Tag> tags, String8 tag_to_find)
{
    TagResult result = {};
    result.result = TagResultEnum::NotFound;
    for (U32 i = 0; i < tags.size; i++)
    {
        if (Str8Cmp(tags.data[i].key, tag_to_find))
        {
            result.result = TagResultEnum::Found;
            result.value = push_str8_copy(arena, tags.data[i].value);
            break;
        }
    }
    return result;
}

EcefLocation
location_get(Network* network, NodeId node_id)
{
    prof_scope_marker;
    EcefLocation* loc = network->ecef_location_map->get(node_id);
    if (loc)
    {
        return *loc;
    }
    Assert("Node not found in hash map");
    return {};
}

WgsLocation
wgs_location_get(Network* network, NodeId node_id)
{
    prof_scope_marker;
    WgsLocation* loc = network->wgs_location_map->get(node_id);
    if (loc)
    {
        return *loc;
    }
    Assert("Node not found in hash map");
    return {};
}

g_internal void
_highway_edges_create(Network* network)
{
    prof_scope_marker;
    Buffer<osm::Way> way_buf = network->ways_arr[enum_idx(osm::WayType::Highway)];

    ChunkList<Edge>* chunk_list = chunk_list_create<Edge>(network->allocator.arena, 1024);
    for (const osm::Way& way : way_buf)
    {
        Edge* prev_edge = 0;
        for (U32 node_idx = 1; node_idx < way.node_count; node_idx++)
        {
            U64 prev_node_id = way.node_ids[node_idx - 1];
            U64 node_id = way.node_ids[node_idx];

            Edge* osm_edge = chunk_list_get_next(network->allocator.arena, chunk_list);
            osm_edge->id = random_u64();
            osm_edge->way_id = way.id;
            osm_edge->node_id_from = prev_node_id;
            osm_edge->node_id_to = node_id;
            osm_edge->prev = prev_edge;

            if (prev_edge)
            {
                prev_edge->next = osm_edge;
            }
            prev_edge = osm_edge;
        }
    }

    Buffer<Edge> edge_buffer = buffer_from_chunk_list(network->allocator.arena, chunk_list);
    Map<S64, Edge*>* edge_map = &network->edge_structure.edge_map;
    for (Edge* edge = edge_buffer.begin(); edge < edge_buffer.end(); edge++)
    {
        edge_map->insert(network->allocator.arena, edge->id, edge);
    }

    network->edge_structure.edges = edge_buffer;
}

g_internal WgsNode*
_wgs_node_find(Buffer<WgsNodeList> node_hashmap, U64 node_id)
{
    WgsNodeList* node_list = &node_hashmap.data[node_id % node_hashmap.size];

    WgsNode* node = node_list->first;
    for (; node; node = node->next)
    {
        if (node->id == node_id)
        {
            break;
        }
    }
    return node;
}

g_internal B32
_node_hashmap_insert(Network* network, NodeId node_id, Way* way, Node** out)
{
    // ~mgj: Insert Node into hash if not already inserted
    U64 node_slot = node_id % network->node_hashmap.size;
    NodeList* slot = &network->node_hashmap.data[node_slot];
    Node* node = slot->first;
    B32 node_inserted = false;
    for (; node; node = node->next)
    {
        if (node->id == node_id)
        {
            break;
        }
    }
    if (!node)
    {
        node = PushStruct(network->allocator.arena, Node);
        node->id = node_id;
        SLLQueuePush(slot->first, slot->last, node);
        Assert(slot->last->next == 0);
        node_inserted = 1;
    }

    WayNode* way_node = PushStruct(network->allocator.arena, WayNode);
    way_node->way = *way;

    // ~mgj: Each node links to the ways that contain it.
    SLLQueuePush(node->way_queue.first, node->way_queue.last, way_node);

    // ~mgj: Ways are put into its hashmap for quick lookup (relevant for pixel picking operations)
    U64 way_id = way->id;
    U64 way_slot = way_id % network->way_hashmap.size;
    WayList* way_list = &network->way_hashmap.data[way_slot];
    SLLQueuePush_N(way_list->first, way_list->last, way_node, hash_next);

    *out = node;
    return node_inserted;
}

g_internal Error
_parse_osm_data(osm::Network* osm_network, String8 file_content)
{
    Arena* arena = osm_network->allocator.arena;
    ScratchScope scratch = ScratchScope(&arena, 1);
    NodeParseResult node_result = wrapper::node_buffer_from_simd_json(osm_network->allocator.arena, file_content, 1000);
    if (node_result.error)
    {
        DEBUG_LOG("Error happend when parsing nodes from json");
        return true;
    }
    WayParseResult osm_way_parse_result = wrapper::way_buffer_from_simd_json(osm_network->allocator.arena, file_content);
    if (osm_way_parse_result.error)
    {
        DEBUG_LOG("Error happend when parsing ways from json");
        return true;
    }

    Buffer<Way> ways = osm_way_parse_result.ways;
    for (U32 way_type_idx = 0; way_type_idx < enum_idx(WayType::Count); ++way_type_idx)
    {
        ChunkList<Way>* way_chunk_list = chunk_list_create<Way>(scratch.arena, 1024);
        ChunkList<NodeId>* node_id_chunk_list = chunk_list_create<NodeId>(scratch.arena, 1024);
        for (U32 way_index = 0; way_index < ways.size; way_index++)
        {
            Way* way = &ways.data[way_index];
            TagResult tag_result = tag_find(scratch.arena, way->tags, S(g_waytype_osm_tag[way_type_idx]));
            if (enum_idx(tag_result.result))
            {
                continue;
            }

            chunk_list_insert(scratch.arena, way_chunk_list, *way);
            for (U32 node_index = 0; node_index < way->node_count; node_index++)
            {
                U64 node_id = way->node_ids[node_index];
                Node* node;
                B8 inserted = _node_hashmap_insert(osm_network, node_id, way, &node);
                if (inserted)
                {
                    WgsNode* node_coord = _wgs_node_find(node_result.node_hashmap, node_id);

                    // Cartographic to ECEF transformation
                    CesiumGeospatial::Cartographic origin_cartographic(glm::radians(node_coord->lon), glm::radians(node_coord->lat), 0);
                    glm::dvec3 coord_ecef = CesiumGeospatial::Ellipsoid::WGS84.cartographicToCartesian(origin_cartographic);
                    EcefLocation loc = ecef_location_create(node_id, vec_3f64(coord_ecef.x, coord_ecef.y, coord_ecef.z));
                    WgsLocation wgs_loc = {.id = node_id, .lat = node_coord->lat, .lon = node_coord->lon};

                    osm_network->ecef_location_map->insert(osm_network->allocator.arena, node_id, loc);
                    osm_network->wgs_location_map->insert(osm_network->allocator.arena, node_id, wgs_loc);

                    chunk_list_insert(scratch.arena, node_id_chunk_list, node_id);
                }
            }
        }

        osm_network->node_id_arr[way_type_idx] = buffer_from_chunk_list(osm_network->allocator.arena, node_id_chunk_list);
        osm_network->ways_arr[way_type_idx] = buffer_from_chunk_list(osm_network->allocator.arena, way_chunk_list);
    }

    _highway_edges_create(osm_network);
    return false;
}
g_internal Error
_network_parse(NetworkLoadTask* input, String8 file_content)
{
    auto network = std::make_unique<Network>(input->node_hashmap_size, input->way_hashmap_size);
    Error error = _parse_osm_data(network.get(), file_content);
    if (!error)
        input->result = std::move(network);
    return error;
}

g_internal async::UserFuncResult<NetworkLoadTask>
_network_download_complete(Arena* arena, async::ThreadPool* thread_pool, String8 response_body, NetworkLoadTask* input)
{
    (void)thread_pool;
    if (cache_write(input->cache.file_location, response_body, input->cache.bbox_str))
        return async::UserFuncResult<NetworkLoadTask>::failure(arena, "Could not cache OSM data");
    if (_network_parse(input, response_body))
        return async::UserFuncResult<NetworkLoadTask>::failure(arena, "Could not parse OSM data");
    return async::UserFuncResult<NetworkLoadTask>::success();
}

g_internal async::AsyncTaskContinuation<NetworkLoadTask>
_network_load_begin(async::ThreadInfo thread_info, async::AsyncTaskStatus<NetworkLoadTask>* task)
{
    (void)thread_info;
    NetworkLoadTask* input = task->user_data;
    Arena* arena = task->allocator.arena;
    ScratchScope scratch = ScratchScope(&arena, 1);
    Result<String8> cached = cache_read(scratch.arena, input->cache.file_location, input->cache.bbox_str);
    if (!cached.err)
    {
        task->error = _network_parse(input, cached.v);
        return {};
    }

    // Attach HTTP to this same task so completion and cleanup retain one owner.
    async::HttpInfo* info = async::http_info_create(arena, HTTP_Method_Post,
                                                    S("http://overpass-api.de/api/interpreter"), S("application/x-www-form-urlencoded"),
                                                    {S("User-Agent: DTCity/0.1"), S("Accept: application/json")});
    info->body = input->request_body;
    auto* http = task->allocator.make<async::AsyncHttpTaskState<NetworkLoadTask>>();
    http->curl_ctx = async::_curl_ctx_create(arena);
    http->thread_pool = task->thread_pool;
    http->first_func = http->next_func = _network_download_complete;
    http->first_http_info = http->next_http_info = info;
    http->max_http_retries = 3;
    http->max_task_retries = 1;
    http->timeout_us = 300'000'000;
    http->task_start_us = os_now_microseconds();
    task->http_ext = http;
    task->ext_type = async::ExtensionType::Http;
    if (task->http_reconfigure(info)) return {};
    return {.func = async::_http_main<NetworkLoadTask>};
}

} // namespace osm
