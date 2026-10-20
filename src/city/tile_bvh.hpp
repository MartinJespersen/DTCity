#pragma once

#include "base/base_allocator.hpp"
#include "base/base_arena.hpp"
#include "base/base_container.hpp"
#include "base/base_core.hpp"
#include "base/base_math.h"
#include "base/container.hpp"
#include "render/render.hpp"
#include "render/tile_bvh.hpp"
#include "render/tile_height.hpp"
#include "shaders/shader_shared.h"
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_double3.hpp>

namespace city
{

// One queryable mesh. Bounds always use the shared city-local coordinate system.
// A Cesium tile with multiple draw batches can have multiple entries.
struct TileMeshBvhEntry
{
    // Assigned by the owner when registering the prepared entry with a TileBvh.
    ArrayResourcePoolHandle handle;
    U64 mesh_revision;
    U32 triangle_count;
    // Registered meshes can remain cached while absent from the query hierarchy.
    // The owner enables only meshes in the current draw list.
    B32 query_enabled;

    Rng3F32 bounds;

    // Zero means the mesh already uses city-local coordinates (identity transform).
    // Otherwise mesh_to_city must be an invertible affine transform.
    B32 has_mesh_transform;
    glm::mat4 mesh_to_city;

    // TileVertex vertices and U32 triangle indices. Triangle-BVH nodes use
    // render::TileBvhGpuNode in mesh space; leaf ranges address this index buffer.
    render::MeshHandle mesh;
    // Owned by the tile batch. Ready with the mesh after the upload completion callback.
    render::Handle triangle_bvh_buffer;
};

struct TileMeshBvhBuild
{
    Buffer<shader::TileBvhGpuNode> nodes;
    Buffer<U32> indices;
};

// Builds a balanced tree and a reordered copy of the triangle indices in arena.
// Does not change input geometry. Empty input produces empty output buffers.
TileMeshBvhBuild
tile_mesh_bvh_build(Arena* arena, Buffer<const render::TileVertex> vertices, Buffer<const U32> indices);

// Prepares metadata from the final city-local mesh on a worker. Does not allocate,
// retain GPU resources, or register the entry. Mesh handles are owned by the tile batch.
TileMeshBvhEntry
tile_mesh_bvh_entry_create(Buffer<const render::TileVertex> vertices, Buffer<const U32> indices,
                          render::MeshHandle mesh, U64 mesh_revision);

// The caller maps request IDs back to agents.
// Rays use city-local coordinates and normalized directions. For height queries,
// start above the surface and point down along the local ellipsoid normal.
// The sampler keeps request IDs on the CPU and packs rays into the shared GPU layout.
struct TileHeightQuery
{
    U64 request_id;
    glm::vec3 ray_origin;
    glm::vec3 ray_direction;
};

struct TileHeightResult
{
    U64 request_id;
    // Closest hit across all entries, in city-local distance units. Valid when hit != 0.
    F32 hit_distance;
    B32 hit;
    // Stable registry identity of the closest mesh; zero on a miss. The entry may
    // have been removed since submission, so this handle must not be dereferenced blindly.
    ArrayResourcePoolHandle entry_handle;
};

struct TileHeightRay
{
    TileHeightQuery query;
    F64 origin_height;
};

// Surface is the city-local ellipsoid point at height zero; up is its unit normal.
// Starts above the complete queryable bounds. Height = origin_height - hit_distance.
TileHeightRay
tile_height_ray_create(glm::dvec3 surface, glm::dvec3 up, Rng3F32 city_bounds);

// The owner thread applies completed tile changes. Workers do not modify this state.
class TileBvh
{
    struct Slot
    {
        TileMeshBvhEntry entry;
        U32 generation;
        U32 next_free;
        B32 occupied;
    };

    // Arrays describe allocated capacity; counts describe the active contents.
    Allocator* allocator = {};
    Array<Slot> slots = {};
    U32 slot_count = {};
    U32 free_slot = {};
    B32 dirty = {};

    // Retain the ready version while its replacement is built and uploaded.
    render::TileBvhHandle ready_resource = {};
    render::TileBvhHandle pending_resource = {};

  public:
    // Borrows the allocator, which must outlive this object and its CPU storage.
    TileBvh(Allocator* allocator);
    TileBvh(const TileBvh&) = delete;
    TileBvh&
    operator=(const TileBvh&) = delete;

    // Retires GPU resources without waiting. Does not clear or release the allocator.
    void
    destroy();

    // Queue owner-thread changes. Copies/retains input buffers until removed or replaced.
    // Only entries with query_enabled != 0 are included in the outer BVH.
    // Only update() starts hierarchy work; these methods do not rebuild the tree.
    ArrayResourcePoolHandle
    insert(const TileMeshBvhEntry& entry);
    void
    replace(const TileMeshBvhEntry& entry);
    void
    remove(ArrayResourcePoolHandle handle);

    // Copies changes and retains their mesh resources. Input spans are borrowed for this call.
    // Replace existing entries by handle. Use insert() to register new entries.
    // Mesh revisions reject stale mesh replacements; bounds, transforms and
    // query selection may change without changing the mesh revision.
    // Removal excludes an entry from the next version; submitted queries retain their resources.
    // Publishes complete versions only and returns the latest ready render resource.
    // Until publication, queries use the previous version, including its removed entries.
    // Also consumes queued insert/replace/remove calls. Call with empty spans to
    // advance pending work. Never waits for workers or the GPU.
    // The returned handle is borrowed until the next update/destroy; submit retains it.
    render::TileBvhHandle
    update(Buffer<const TileMeshBvhEntry> changed_entries,
           Buffer<const ArrayResourcePoolHandle> removed_entries);

  private:
    void
    _entry_buffers_retain(const TileMeshBvhEntry& entry);
    void
    _entry_buffers_release(const TileMeshBvhEntry& entry);
};

// Submission and collection run on the owner thread. GPU completion is polled, never awaited.
// Samples query-enabled uploaded geometry. Does not request tiles or force a finer LOD.
class TileHeightSampler
{
    struct Batch
    {
        render::TileHeightBatchHandle handle;
        Array<U64> request_ids; // CPU metadata; shader results use the query index.
        U32 query_count;
        U32 collected_count;
        B32 in_flight;
    };

    Allocator* allocator = {};
    Batch batches[render::MAX_FRAMES_IN_FLIGHT] = {};
    U32 query_capacity = {};
    U32 submit_batch_index = {};
    U32 collect_batch_index = {};

  public:
    // Accepts an allocator to avoid dependencies on other layers.
    // The allocator must outlive this object and its CPU storage.
    TileHeightSampler(Allocator* allocator, U32 query_capacity);

    TileHeightSampler(const TileHeightSampler&) = delete;
    TileHeightSampler&
    operator=(const TileHeightSampler&) = delete;

    // Discards pending results and retires GPU resources without waiting.
    void
    destroy();

    // Copies requests and retains the complete BVH resource for the submitted work.
    // Requires queries.size <= query_capacity and normalized ray directions.
    // Tests all candidate tiles and returns the closest hit, not the first tile hit.
    // Transform ray directions without renormalizing so hit distances remain comparable.
    // Returns false if the resource is not ready or no submission slot is available.
    // Does not wait for the GPU. A ready empty BVH produces misses.
    B32
    submit(render::TileBvhHandle bvh_resource, Buffer<const TileHeightQuery> queries);

    // Copies completed results into caller-owned storage and returns the number written.
    // Uncollected results remain available. Zero means no results were copied.
    U32
    collect(Buffer<TileHeightResult> results);
};

} // namespace city
