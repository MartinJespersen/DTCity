#include "tile_bvh.hpp"
#include "base/base_core.hpp"
#include "base/base_arena.hpp"
#include "base/base_math.h"
#include "base/base_thread_context.h"
#include "base/container.hpp"
#include "base/container_templates.hpp"
#include <algorithm>

namespace render
{

struct TileBvhBuildItem
{
    U32 source_index;
    Rng3F32 bounds;
    Vec3F32 centroid;
};

TileBvhBuild
tile_bvh_nodes_build(Arena* arena, Buffer<const Rng3F32> bounds, U32 leaf_item_limit)
{
    Assert(leaf_item_limit > 0);
    Assert(bounds.size <= max_U32 / 2);
    TileBvhBuild result = {};
    U32 item_count = (U32)bounds.size;
    if (item_count == 0)
        return result;
    ScratchScope scratch = ScratchScope(&arena, 1);
    Buffer<TileBvhBuildItem> items = buffer_alloc<TileBvhBuildItem>(scratch.arena, item_count);
    result.nodes = buffer_alloc<TileBvhGpuNode>(arena, (U64)item_count * 2 - 1);
    result.item_indices = buffer_alloc<U32>(arena, item_count);
    for (U32 item_idx = 0; item_idx < item_count; ++item_idx)
    {
        items.data[item_idx].source_index = item_idx;
        items.data[item_idx].bounds = bounds.data[item_idx];
        for (U32 axis = 0; axis < 3; ++axis)
            items.data[item_idx].centroid.v[axis] = bounds.data[item_idx].min.v[axis] * 0.5f +
                                                  bounds.data[item_idx].max.v[axis] * 0.5f;
    }
    // Process nodes iteratively. Median splits bound tree depth even when all
    // centroids coincide. Each internal node reserves adjacent child records.
    U32 node_count = 1;
    result.nodes.data[0].first = 0;
    result.nodes.data[0].count = item_count;
    for (U32 node_idx = 0; node_idx < node_count; ++node_idx)
    {
        TileBvhGpuNode& node = result.nodes.data[node_idx];
        U32 first = node.first;
        U32 count = node.count;
        Rng3F32 node_bounds = items.data[first].bounds;
        Rng3F32 centroid_bounds = {items.data[first].centroid, items.data[first].centroid};
        for (U32 item_idx = first + 1; item_idx < first + count; ++item_idx)
        {
            const TileBvhBuildItem& item = items.data[item_idx];
            for (U32 axis = 0; axis < 3; ++axis)
            {
                node_bounds.min.v[axis] = Min(node_bounds.min.v[axis], item.bounds.min.v[axis]);
                node_bounds.max.v[axis] = Max(node_bounds.max.v[axis], item.bounds.max.v[axis]);
                centroid_bounds.min.v[axis] = Min(centroid_bounds.min.v[axis], item.centroid.v[axis]);
                centroid_bounds.max.v[axis] = Max(centroid_bounds.max.v[axis], item.centroid.v[axis]);
            }
        }
        for (U32 axis = 0; axis < 3; ++axis)
        {
            node.bounds_min[axis] = node_bounds.min.v[axis];
            node.bounds_max[axis] = node_bounds.max.v[axis];
        }
        if (count <= leaf_item_limit)
            continue;

        U32 split_axis = 0;
        for (U32 axis = 1; axis < 3; ++axis)
        {
            if (centroid_bounds.max.v[axis] - centroid_bounds.min.v[axis] >
                centroid_bounds.max.v[split_axis] - centroid_bounds.min.v[split_axis])
                split_axis = axis;
        }
        U32 left_count = count / 2;
        std::nth_element(items.data + first, items.data + first + left_count,
                         items.data + first + count,
                         [split_axis](const TileBvhBuildItem& a, const TileBvhBuildItem& b)
                         {
                             F32 a_center = a.centroid.v[split_axis];
                             F32 b_center = b.centroid.v[split_axis];
                             return a_center == b_center ? a.source_index < b.source_index : a_center < b_center;
                         });
        node.first = node_count;
        node.count = 0;
        result.nodes.data[node_count].first = first;
        result.nodes.data[node_count].count = left_count;
        result.nodes.data[node_count + 1].first = first + left_count;
        result.nodes.data[node_count + 1].count = count - left_count;
        node_count += 2;
    }
    result.nodes.size = node_count;

    for (U32 item_idx = 0; item_idx < item_count; ++item_idx)
        result.item_indices.data[item_idx] = items.data[item_idx].source_index;
    return result;
}

} // namespace render
