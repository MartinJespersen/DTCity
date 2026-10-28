#pragma once

#include "base/base_inc.hpp"
#include "async/async_task.hpp"
#include "neta.hpp"
#include "tessellation.hpp"
#include <memory>

namespace city
{
struct RoadCrossSection
{
    Vec2F64 top;
    Vec2F64 btm;
    osm::EcefLocation node;
};

struct RoadSegment
{
    RoadCrossSection start;
    RoadCrossSection end;
};

struct RoadBuildInput
{
    // Borrowed until completion; the city drains its tasks before releasing networks.
    osm::Network* network;
    String8 netascore_file_path;
    Rng2F64 bbox;
    glm::dmat4 ecef_to_local;
    F32 default_road_width;
};

struct Road
{
    Allocator allocator;
    Map<osm::EdgeId, RoadInfo>* road_info_map = {};
    // Private until publication; the tile loader then owns the BVH arena lifetime.
    RoadBvh pending_bvh = {};
    PoolHandle bvh_handle = {};
    RoadOverlayOption overlay_option_cur = {};
    bool ready = {};
    render::Handle colormap_handle = {};
    render::Handle segment_buffer_handle = {};

    Road() noexcept;
    Road(Road&& other) noexcept;
    ~Road() noexcept;
};

struct RoadBuildTask
{
    RoadBuildInput input;
    std::unique_ptr<Road> result;
};

class RoadContext
{
    ResourcePool<Road>* roads = {};

  public:
    void init(Arena* arena, U32 capacity) noexcept;
    async::AsyncTaskStatus<RoadBuildTask>* create(async::ThreadPool* thread_pool, const RoadBuildInput& input) noexcept;
    PoolHandle publish(RoadBuildTask* task, ResourcePool<RoadBvh>* bvh_pool) noexcept;
    bool get(PoolHandle handle, Road** out_road) noexcept;
    void pipeline_add(PoolHandle handle, render::TilePipelineData* pipeline, RoadOverlayOption overlay) noexcept;
    bool update(PoolHandle handle, RoadOverlayOption overlay) noexcept;
    void release(PoolHandle handle, ResourcePool<RoadBvh>* bvh_pool) noexcept;
};

// Private geometry and task stages.
g_internal async::AsyncTaskContinuation<RoadBuildTask>
_road_build(async::ThreadInfo info, async::AsyncTaskStatus<RoadBuildTask>* status);
g_internal void
_road_build_cpu(Arena* arena, Road* road, const RoadBuildInput& input);
g_internal void
_road_upload(Road* road);
g_internal void
_road_assert_non_intersecting_edges(glm::vec2* vertices, U32 count);
g_internal Buffer<RoadSegmentCorners>
_road_corners_build(Arena* arena, osm::Network* network, Buffer<osm::Edge> edge_buffer, F32 default_road_width,
                   const glm::dmat4& ecef_to_local, Map<osm::EdgeId, RoadInfo>* road_info_map);
g_internal Map<osm::EdgeId, RoadInfo>*
_road_info_map_create(Arena* arena, osm::Network* network, Buffer<osm::Edge> edge_buffer,
                       Map<S64, neta::EdgeList>* neta_edge_map);
g_internal neta::Edge*
_netascore_edge_from_osm_edge(osm::Network* network, osm::Edge* osm_edge, Map<S64, neta::EdgeList>* edge_list_map);
g_internal void
_road_segment_from_nodes(RoadSegment* out_road_segment, osm::EcefLocation node_0, osm::EcefLocation node_1,
                        F32 road_width);
g_internal void
_road_quick_select(Buffer<BoundingBox> center_buffer, U32 split_axis, U32 start_idx, U32 end_idx, U32 k);
g_internal Rng2F32
_road_bounds_union(Rng2F32 a, Rng2F32 b);
g_internal Rng2F32
_road_bounds_union(Rng2F32 rng, Vec2F32 vec);
g_internal Rng2F32
_road_bounds_union(Buffer<BoundingBox> center_buffer, U32 start_idx, U32 end_idx);
g_internal Axis2
_road_split_axis_find(Buffer<BoundingBox> bb_buffer, U32 start_idx, U32 end_idx);
g_internal void
_road_bvh_create(RoadBvh* bvh, Buffer<RoadSegmentCorners> road_segment_buffer, U32 leaf_bb_max);
} // namespace city
