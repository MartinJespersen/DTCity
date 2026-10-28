#include "core_inc.hpp"
#include "async/async_inc.hpp"
#include "city/road.hpp"
#include "generated/colormaps.h"

namespace city
{
Road::Road() noexcept
    : allocator(Allocator::create({.reserve_size = GB(4), .commit_size = arena_default_commit_size,
                                   .flags = arena_default_flags}))
{
    Debug_SetName(this->allocator.arena, "city road data");
    this->pending_bvh.arena = arena_alloc();
    Debug_SetName(this->pending_bvh.arena, "city road BVH");
}

Road::Road(Road&& other) noexcept
    : allocator(std::move(other.allocator)), road_info_map(other.road_info_map),
      pending_bvh(std::exchange(other.pending_bvh, {})), bvh_handle(std::exchange(other.bvh_handle, {})),
      overlay_option_cur(other.overlay_option_cur), ready(other.ready),
      colormap_handle(std::exchange(other.colormap_handle, {})),
      segment_buffer_handle(std::exchange(other.segment_buffer_handle, {}))
{
}

Road::~Road() noexcept
{
    // Unpublished results own their BVH. Published BVHs are released by RoadContext/tile loading.
    if (this->pending_bvh.arena) arena_release(this->pending_bvh.arena);
    render::handle_destroy_deferred(this->colormap_handle);
    render::handle_destroy_deferred(this->segment_buffer_handle);
}

void
RoadContext::init(Arena* arena, U32 capacity) noexcept
{
    this->roads = ResourcePool<Road>::create(arena, capacity);
}

async::AsyncTaskStatus<RoadBuildTask>*
RoadContext::create(async::ThreadPool* thread_pool, const RoadBuildInput& input) noexcept
{
    Allocator allocator = Allocator::create();
    Debug_SetName(allocator.arena, "road build task");
    RoadBuildTask* task = allocator.make<RoadBuildTask>();
    task->input = input;
    task->input.netascore_file_path = push_str8_copy(allocator.arena, input.netascore_file_path);
    return async::async_task_run(std::move(allocator), thread_pool, _road_build, task, "Road build task");
}

PoolHandle
RoadContext::publish(RoadBuildTask* task, ResourcePool<RoadBvh>* bvh_pool) noexcept
{
    Assert(task->result);
    PoolHandle handle = this->roads->emplace(std::move(*task->result));
    Road* road = {};
    this->roads->item_from_handle(handle, &road);
    RoadBvh* bvh = {};
    road->bvh_handle = bvh_pool->item_new(&bvh);
    *bvh = std::exchange(road->pending_bvh, {});
    return handle;
}

bool
RoadContext::get(PoolHandle handle, Road** out_road) noexcept
{
    return this->roads->item_from_handle(handle, out_road);
}

void
RoadContext::pipeline_add(PoolHandle handle, render::TilePipelineData* pipeline, RoadOverlayOption overlay) noexcept
{
    Road* road = {};
    bool available = this->roads->item_from_handle(handle, &road) && road->ready;
    pipeline->pipeline_bits &= ~render::TilePipelineBits::ColormapEnable;
    if (available && overlay != RoadOverlayOption_None)
        pipeline->pipeline_bits |= render::TilePipelineBits::ColormapEnable;
    pipeline->colormap_handle = available ? road->colormap_handle : render::Handle{};
    pipeline->road_segment_buffer_handle = available ? road->segment_buffer_handle : render::Handle{};
    pipeline->road_test_enabled = available && overlay != RoadOverlayOption_None;
    pipeline->overlay_option_idx = overlay;
    render::tile_pipeline_add(pipeline);
}

bool
RoadContext::update(PoolHandle handle, RoadOverlayOption overlay) noexcept
{
    Road* road = {};
    if (!this->roads->item_from_handle(handle, &road)) return false;
    road->overlay_option_cur = overlay;
    if (render::is_handle_zero(road->segment_buffer_handle)) return false;
    if (!road->ready)
    {
        // CPU completion does not imply that the submitted GPU uploads have completed.
        road->ready = render::is_resource_loaded(road->colormap_handle) &&
            render::is_resource_loaded(road->segment_buffer_handle);
    }
    return road->ready;
}

void
RoadContext::release(PoolHandle handle, ResourcePool<RoadBvh>* bvh_pool) noexcept
{
    Road* road = {};
    if (!this->roads->item_from_handle(handle, &road)) return;
    RoadBvh* bvh = {};
    if (bvh_pool->item_from_handle(road->bvh_handle, &bvh))
    {
        bvh->deletion_requested = true;
        if (bvh->loads_in_flight == 0)
        {
            arena_release(bvh->arena);
            bvh_pool->item_free(road->bvh_handle);
        }
    }
    this->roads->item_free(handle);
}

g_internal async::AsyncTaskContinuation<RoadBuildTask>
_road_build(async::ThreadInfo info, async::AsyncTaskStatus<RoadBuildTask>* status)
{
    (void)info;
    RoadBuildTask* task = status->user_data;
    auto road = std::make_unique<Road>();
    Arena* arena = road->allocator.arena;
    ScratchScope scratch = ScratchScope(&arena, 1);
    _road_build_cpu(scratch.arena, road.get(), task->input);
    _road_upload(road.get());
    task->result = std::move(road);
    return {};
}

g_internal void
_road_build_cpu(Arena* arena, Road* road, const RoadBuildInput& input)
{
    // Intermediate Neta geometry lives only for this worker step.
    Map<S64, neta::EdgeList>* neta_edges =
        neta::osm_way_to_edges_map_create(arena, input.network, input.netascore_file_path, input.bbox);
    road->road_info_map = _road_info_map_create(road->allocator.arena, input.network,
        input.network->edge_structure.edges, neta_edges);
    Buffer<RoadSegmentCorners> corners = _road_corners_build(arena, input.network, input.network->edge_structure.edges,
        input.default_road_width, input.ecef_to_local, road->road_info_map);
    _road_bvh_create(&road->pending_bvh, corners, 10);
}

g_internal void
_road_upload(Road* road)
{
    if (road->pending_bvh.road_segment_buffer_sorted.size == 0) return;
    render::ThreadWorkerCmdCtx* thread = render::thread_ctx_create();
    render::thread_cmd_buffer_record(thread);
    defer(render::thread_cmd_buffer_end(thread));
    // All staging copies finish before the worker scratch scope ends.
    render::BufferInfo colormap_info(Buffer<U8>((U8*)g_colormap_inferno, sizeof(g_colormap_inferno)),
                                      render::BufferType_StorageBuffer);
    render::BufferInfo segment_info(road->pending_bvh.road_segment_buffer_sorted, render::BufferType_StorageBuffer);
    road->colormap_handle = render::buffer_load_sync(thread, &colormap_info, S("road colormap"));
    road->segment_buffer_handle = render::buffer_load_sync(thread, &segment_info, S("road segments"));
}

g_internal void
_road_segment_from_nodes(RoadSegment* out_road_segment, osm::EcefLocation node_0, osm::EcefLocation node_1,
                        F32 road_width)
{
    Vec2F64 road_0_pos = node_0.pos.xy;
    Vec2F64 road_1_pos = node_1.pos.xy;
    Vec2F64 road_dir = sub_2f64(road_1_pos, road_0_pos);
    Vec2F64 orthogonal_vec = vec_2f64(road_dir.y, -road_dir.x);
    Vec2F64 normal_scaled = vec_2f64(0.001f, 0.001f);
    if (orthogonal_vec.x != 0 && orthogonal_vec.y != 0)
    {
        Vec2F64 normal = normalize_2f64(orthogonal_vec);
        normal_scaled = scale_2f64(normal, road_width / 2.0f);
    }

    out_road_segment->start.top = add_2f64(road_0_pos, normal_scaled);
    out_road_segment->start.btm = sub_2f64(road_0_pos, normal_scaled);
    out_road_segment->end.top = add_2f64(road_1_pos, normal_scaled);
    out_road_segment->end.btm = sub_2f64(road_1_pos, normal_scaled);

    out_road_segment->start.node = node_0;
    out_road_segment->end.node = node_1;
}

// BVH functions /////////////////////////////

g_internal void
_road_quick_select(Buffer<BoundingBox> center_buffer, U32 split_axis, U32 start_idx, U32 end_idx, U32 k)
{
    Assert(k < center_buffer.size);

    while (start_idx < end_idx)
    {
        U32 pivot_idx = end_idx - 1;
        F32 pivot = center_buffer.data[pivot_idx].center.v[split_axis];

        U32 divider_idx = start_idx;
        for (U32 i = start_idx; i < pivot_idx; ++i)
        {
            if (center_buffer.data[i].center.v[split_axis] < pivot)
            {
                Swap(BoundingBox, center_buffer.data[i], center_buffer.data[divider_idx]);
                divider_idx++;
            }
        }

        Swap(BoundingBox, center_buffer.data[divider_idx], center_buffer.data[pivot_idx]);

        if (divider_idx == k)
        {
            break;
        }
        else if (divider_idx < k)
        {
            start_idx = divider_idx + 1;
        }
        else
        {
            end_idx = divider_idx;
        }
    }
}

g_internal Rng2F32
_road_bounds_union(Rng2F32 a, Rng2F32 b)
{
    Rng2F32 result;
    result.min.v[0] = Min(a.min.v[0], b.min.v[0]);
    result.min.v[1] = Min(a.min.v[1], b.min.v[1]);

    result.max.v[0] = Max(a.max.v[0], b.max.v[0]);
    result.max.v[1] = Max(a.max.v[1], b.max.v[1]);
    return result;
}

g_internal Rng2F32
_road_bounds_union(Rng2F32 rng, Vec2F32 vec)
{
    Rng2F32 result;
    result.min.v[0] = Min(rng.min.v[0], vec.v[0]);
    result.min.v[1] = Min(rng.min.v[1], vec.v[1]);

    result.max.v[0] = Max(rng.max.v[0], vec.v[0]);
    result.max.v[1] = Max(rng.max.v[1], vec.v[1]);
    return result;
}

g_internal Rng2F32
_road_bounds_union(Buffer<BoundingBox> center_buffer, U32 start_idx, U32 end_idx)
{
    Rng2F32 bounds = rng2f32_inverted_inf();
    for (U32 i = start_idx; i < end_idx; ++i)
    {
        BoundingBox seg_center = center_buffer.data[i];
        bounds = _road_bounds_union(bounds, seg_center.bounds);
    }
    return bounds;
}

g_internal Axis2
_road_split_axis_find(Buffer<BoundingBox> bb_buffer, U32 start_idx, U32 end_idx)
{
    Rng2F32 bounds = rng2f32_inverted_inf();
    for (U32 i = start_idx; i < end_idx; ++i)
    {
        BoundingBox* seg_center = &bb_buffer[i];
        for (U32 ax_idx = 0; ax_idx < Axis2_COUNT; ++ax_idx)
        {
            if (seg_center->center.v[ax_idx] < bounds.min.v[ax_idx])
            {
                bounds.min.v[ax_idx] = seg_center->center.v[ax_idx];
            }

            if (seg_center->center.v[ax_idx] > bounds.max.v[ax_idx])
            {
                bounds.max.v[ax_idx] = seg_center->center.v[ax_idx];
            }
        }
    }

    Vec2F32 diff = sub_2f32(bounds.max, bounds.min);
    Axis2 split_axis = diff.x > diff.y ? Axis2_X : Axis2_Y;
    return split_axis;
}

g_internal void
_road_bvh_create(RoadBvh* bvh, Buffer<RoadSegmentCorners> road_segment_buffer, U32 leaf_bb_max)
{
    if (road_segment_buffer.size == 0) return;
    Arena* arena = bvh->arena;
    ScratchScope scratch = ScratchScope(&arena, 1);
    Buffer<BoundingBox> bb_buffer = buffer_alloc<BoundingBox>(scratch.arena, road_segment_buffer.size);

    // 1. for every element in the buffer, find the center point (used for segmentation)
    for (U32 i = 0; i < bb_buffer.size; i++)
    {
        // init idx to later point to RoadSegmentCorners*
        bb_buffer.data[i].idx = i;

        // create bounding box around road segment
        Rng2F32 bounds = rng2f32_inverted_inf();
        for (U32 j = 0; j < Corner_COUNT; ++j)
        {
            RoadSegmentCorners* seg = &road_segment_buffer[i];
            Vec2F32 corner = seg->corners[j];
            bounds = _road_bounds_union(bounds, corner);
        }
        bb_buffer.data[i].bounds = bounds;

        // find center point
        Vec2F32 center = {};
        for (U32 j = 0; j < Corner_COUNT; ++j)
        {
            RoadSegmentCorners* seg = &road_segment_buffer[i];
            Vec2F32 corner = seg->corners[j];
            center = add_2f32(center, corner);
        }
        bb_buffer.data[i].center = scale_2f32(center, 1.0f / (F32)Corner_COUNT);
    }

    RoadBvhContext* bvh_ctx = PushStruct(scratch.arena, RoadBvhContext);
    bvh_ctx->road_segment_buffer = road_segment_buffer;
    bvh_ctx->bb_buffer = bb_buffer;

    RoadSegmentNode* root = PushStruct(bvh->arena, RoadSegmentNode);
    bvh_ctx->root = root;
    root->bounds = _road_bounds_union(bb_buffer, 0, bb_buffer.size);
    root->start_idx = 0;
    root->end_idx = bb_buffer.size;

    SLLStackPush(bvh_ctx->stack, root);

    Buffer<RoadSegmentCorners> road_segment_buffer_sorted =
        buffer_alloc<RoadSegmentCorners>(bvh->arena, bvh_ctx->road_segment_buffer.size);
    while (bvh_ctx->stack)
    {
        RoadSegmentNode* node = bvh_ctx->stack;
        SLLStackPop(bvh_ctx->stack);

        U32 num_elem = node->end_idx - node->start_idx;

        if (num_elem <= leaf_bb_max)
        {
            for (U32 i = node->start_idx; i < node->end_idx; ++i)
            {
                // copy road segment corners from bb_buffer index to sorted buffer
                BoundingBox bb = bvh_ctx->bb_buffer.data[i];
                road_segment_buffer_sorted.data[i] = bvh_ctx->road_segment_buffer.data[bb.idx];
            }
            continue;
        }

        Axis2 split_axis = _road_split_axis_find(bvh_ctx->bb_buffer, node->start_idx, node->end_idx);
        U32 split_idx = node->start_idx + (num_elem) / 2;
        _road_quick_select(bb_buffer, (U32)split_axis, node->start_idx, node->end_idx, split_idx);

        Rng2F32 idx_range = rng_2f32(V2F32(node->start_idx, split_idx), V2F32(split_idx, node->end_idx));

        for (U32 i = 0; i < ArrayCount(idx_range.v); ++i)
        {
            node->children[i] = PushStruct(bvh->arena, RoadSegmentNode);
            node->children[i]->start_idx = idx_range.min.v[i];
            node->children[i]->end_idx = idx_range.max.v[i];
            node->children[i]->bounds =
                _road_bounds_union(bb_buffer, node->children[i]->start_idx, node->children[i]->end_idx);
        }

        SLLStackPush(bvh_ctx->stack, node->children[1]);
        SLLStackPush(bvh_ctx->stack, node->children[0]);
    }

    bvh->root = bvh_ctx->root;
    bvh->road_segment_buffer_sorted = road_segment_buffer_sorted;
}

g_internal void
_road_assert_non_intersecting_edges(glm::vec2* vertices, U32 count)
{
    AssertAlways(count >= 4);
    glm::vec2 v0 = vertices[0];
    glm::vec2 v1 = vertices[1];
    glm::vec2 v2 = vertices[2];
    glm::vec2 v3 = vertices[3];

    glm::vec2 _dummy;
    AssertAlways(geometry::line_intersection(v0, v1, v2, v3, &_dummy) == false);
    AssertAlways(geometry::line_intersection(v1, v2, v3, v0, &_dummy) == false);
}

g_internal Buffer<RoadSegmentCorners>
_road_corners_build(Arena* arena, osm::Network* network, Buffer<osm::Edge> edge_buffer, F32 default_road_width,
                   const glm::dmat4& ecef_to_local, Map<osm::EdgeId, RoadInfo>* road_info_map)
{
    prof_scope_marker;

    Buffer<RoadSegmentCorners> corner_buffer = buffer_alloc<RoadSegmentCorners>(arena, edge_buffer.size);

    for (U32 i = 0; i < edge_buffer.size; i++)
    {
        osm::Edge* edge = &edge_buffer[i];

        osm::EcefLocation start_node = osm::location_get(network, edge->node_id_from);
        osm::EcefLocation end_node = osm::location_get(network, edge->node_id_to);

        RoadSegment road_segment;
        _road_segment_from_nodes(&road_segment, start_node, end_node, default_road_width);

        // Road coordinates stored in buffer for 3D geometry projection
        RoadSegmentCorners* road_segment_corners = &corner_buffer[i];
        road_segment_corners->edge_id = edge->id;
        glm::vec2 local_top_left =
            glm::vec2(ecef_to_local * glm::dvec4(road_segment.start.top.x, road_segment.start.top.y,
                                                 road_segment.start.node.pos.z, 1.0));
        glm::vec2 local_top_right = glm::vec2(ecef_to_local * glm::dvec4(road_segment.end.top.x, road_segment.end.top.y,
                                                                         road_segment.end.node.pos.z, 1.0));
        glm::vec2 local_bottom_right =
            glm::vec2(ecef_to_local *
                      glm::dvec4(road_segment.end.btm.x, road_segment.end.btm.y, road_segment.end.node.pos.z, 1.0));
        glm::vec2 local_bottom_left =
            glm::vec2(ecef_to_local * glm::dvec4(road_segment.start.btm.x, road_segment.start.btm.y,
                                                 road_segment.start.node.pos.z, 1.0));
        road_segment_corners->corners[RoadSegmentCornerCoord_TopLeft] = vec_2f32(local_top_left.x, local_top_left.y);
        road_segment_corners->corners[RoadSegmentCornerCoord_TopRight] = vec_2f32(local_top_right.x, local_top_right.y);
        road_segment_corners->corners[RoadSegmentCornerCoord_BottomRight] =
            vec_2f32(local_bottom_right.x, local_bottom_right.y);
        road_segment_corners->corners[RoadSegmentCornerCoord_BottomLeft] =
            vec_2f32(local_bottom_left.x, local_bottom_left.y);

        // Assert non_intersecting edges
        {
            glm::vec2 vertices[RoadSegmentCornerCoord_Count] = {local_top_left, local_top_right, local_bottom_right,
                                                                local_bottom_left};
            _road_assert_non_intersecting_edges(vertices, RoadSegmentCornerCoord_Count);
        }
        RoadInfo* road_info = road_info_map->get(edge->id);
        if (road_info)
        {
            road_segment_corners->road_info = *road_info;
        }

    }

    return corner_buffer;
}

g_internal Map<osm::EdgeId, RoadInfo>*
_road_info_map_create(Arena* arena, osm::Network* network, Buffer<osm::Edge> edge_buffer,
                       Map<S64, neta::EdgeList>* neta_edge_map)
{
    prof_scope_marker;
    Map<osm::EdgeId, RoadInfo>* road_info_map = Map<osm::EdgeId, RoadInfo>::create(arena, 1024);

    for (osm::Edge& edge : edge_buffer)
    {
        neta::Edge* neta_edge = _netascore_edge_from_osm_edge(network, &edge, neta_edge_map);
        if (neta_edge)
        {
            RoadInfo info = {};
            info.options[RoadOverlayOption_Bikeability_ft] = neta_edge->index_bike_ft;
            info.options[RoadOverlayOption_Bikeability_tf] = neta_edge->index_bike_tf;
            info.options[RoadOverlayOption_Walkability_ft] = neta_edge->index_walk_ft;
            info.options[RoadOverlayOption_Walkability_tf] = neta_edge->index_walk_tf;
            road_info_map->insert(arena, edge.id, info);
        }
    }

    return road_info_map;
}

g_internal neta::Edge*
_netascore_edge_from_osm_edge(osm::Network* network, osm::Edge* osm_edge, Map<osm::WayId, neta::EdgeList>* edge_list_map)
{
    S64 from_id = osm_edge->node_id_from;
    S64 to_id = osm_edge->node_id_to;

    osm::WgsLocation from_node_loc = osm::wgs_location_get(network, from_id);
    osm::WgsLocation to_node_loc = osm::wgs_location_get(network, to_id);
    Vec2F64 from_node_coord = vec_2f64(from_node_loc.lon, from_node_loc.lat);
    Vec2F64 to_node_coord = vec_2f64(to_node_loc.lon, to_node_loc.lat);

    S64 way_id = osm_edge->way_id;
    neta::EdgeList* edge_list = edge_list_map->get(way_id);

    neta::Edge* chosen_edge = {};
    if (edge_list)
    {
        F64 smallest_dist = max_f64;
        for (neta::EdgeNode* edge_node = edge_list->first; edge_node; edge_node = edge_node->next)
        {
            neta::Edge* edge = edge_node->edge;
            for (Vec2F64& coord : edge->coords)
            {
                F64 from_dist = dist_2f64(coord, from_node_coord);
                F64 to_dist = dist_2f64(coord, to_node_coord);
                F64 closest_dist = Min(from_dist, to_dist);
                if (closest_dist < smallest_dist)
                {
                    smallest_dist = closest_dist;
                    chosen_edge = edge;
                }
            }
        }
    }
    return chosen_edge;
}
} // namespace city
