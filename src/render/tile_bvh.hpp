#pragma once

#include "base/base_core.hpp"
#include "base/base_arena.hpp"
#include "base/base_math.h"
#include "base/container.hpp"
#include "render/render.hpp"
#include "shaders/shader_shared.h"

namespace render
{

// Identifies an immutable render-layer resource containing the outer BVH,
// mesh descriptors, and retained vertex, index, and triangle-BVH buffers.
// A zero generation means no ready resource. This CPU handle is not shader data.
struct TileBvhHandle
{
    U32 idx;
    U32 gen_id;
};

// Preserve the render-layer names while C++ and GLSL use one definition.
using TileBvhGpuNode = shader::TileBvhGpuNode;
using TileBvhGpuEntry = shader::TileBvhGpuEntry;

struct TileBvhBuild
{
    Buffer<TileBvhGpuNode> nodes;
    Buffer<U32> item_indices;
};

// Pure median-split builder. Leaves address ranges of the returned permutation.
TileBvhBuild
tile_bvh_nodes_build(Arena* arena, Buffer<const Rng3F32> bounds, U32 leaf_item_limit);

struct TileBvhMeshBuffers
{
    MeshHandle mesh;
    Handle triangle_bvh;
};

struct TileBvhResourceView
{
    Handle node_buffer;
    Handle entry_buffer;
    Handle mesh_address_buffer;
    U32 node_count;
    U32 entry_count;
    Rng3F32 city_bounds;
};

// Owner-thread API. Copies inputs, retains meshes, and builds/uploads on a worker.
// A zero handle means worker submission failed; the caller can retry next frame.
TileBvhHandle
tile_bvh_resource_create(Buffer<const Rng3F32> bounds, Buffer<const TileBvhGpuEntry> entries,
                         Buffer<const TileBvhMeshBuffers> meshes);
// Returns false until upload completion. The view is borrowed while the resource
// is retained. A ready empty resource has no buffers and zero entries/nodes.
B32
tile_bvh_resource_get(TileBvhHandle handle, TileBvhResourceView* view);
void
tile_bvh_resource_retain(TileBvhHandle handle);
void
tile_bvh_resource_destroy(TileBvhHandle handle);

} // namespace render
