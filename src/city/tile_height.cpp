#include "tile_bvh.hpp"
#include "base/base_core.hpp"
#include "base/base_arena.hpp"
#include "base/base_math.h"
#include "base/base_allocator.hpp"
#include "base/base_thread_context.h"
#include "base/container.hpp"
#include "base/container_templates.hpp"
#include "render/render.hpp"
#include "render/tile_bvh.hpp"
#include "render/tile_height.hpp"
#include "shaders/shader_shared.h"
#include <glm/ext/vector_double3.hpp>
#include <glm/ext/vector_float3.hpp>

namespace city
{

TileHeightRay
tile_height_ray_create(glm::dvec3 surface, glm::dvec3 up, Rng3F32 city_bounds)
{
    F64 projection = 0.0;
    for (U32 axis = 0; axis < 3; ++axis)
    {
        F64 extreme = up[axis] >= 0 ? city_bounds.max.v[axis] : city_bounds.min.v[axis];
        projection += (extreme - surface[axis]) * up[axis];
    }
    TileHeightRay ray = {};
    ray.origin_height = Max(10.0, projection + 1.0);
    ray.query.ray_origin = glm::vec3(surface + up * ray.origin_height);
    ray.query.ray_direction = glm::vec3(-up);
    return ray;
}

TileHeightSampler::TileHeightSampler(Allocator* allocator, U32 query_capacity)
    : allocator(allocator), query_capacity(query_capacity)
{
    Assert(query_capacity > 0);
    for (Batch& batch : this->batches)
    {
        batch.handle = render::tile_height_batch_create(query_capacity);
        batch.request_ids = Array<U64>(allocator, query_capacity);
    }
}

void
TileHeightSampler::destroy()
{
    for (Batch& batch : this->batches)
    {
        render::tile_height_batch_destroy(batch.handle);
        batch.handle = {};
        batch.request_ids = {};
        batch.in_flight = false;
    }
    this->query_capacity = 0;
}

B32
TileHeightSampler::submit(render::TileBvhHandle bvh_resource, Buffer<const TileHeightQuery> queries)
{
    Assert(queries.size <= this->query_capacity);
    if (queries.size == 0)
        return true;
    Arena* arena = this->allocator->arena;
    ScratchScope scratch = ScratchScope(&arena, 1);
    Buffer<shader::TileHeightGpuQuery> packed = buffer_alloc<shader::TileHeightGpuQuery>(scratch.arena, queries.size);
    for (U32 index = 0; index < queries.size; ++index)
        for (U32 axis = 0; axis < 3; ++axis)
        {
            packed.data[index].origin[axis] = queries.data[index].ray_origin[axis];
            packed.data[index].direction[axis] = queries.data[index].ray_direction[axis];
        }
    for (U32 offset = 0; offset < render::MAX_FRAMES_IN_FLIGHT; ++offset)
    {
        U32 index = (this->submit_batch_index + offset) % render::MAX_FRAMES_IN_FLIGHT;
        Batch& batch = this->batches[index];
        if (batch.in_flight)
            continue;
        if (!render::tile_height_batch_submit(batch.handle, bvh_resource, {packed.data, packed.size}))
            return false;
        for (U32 query_index = 0; query_index < queries.size; ++query_index)
            batch.request_ids.data[query_index] = queries.data[query_index].request_id;
        batch.query_count = (U32)queries.size;
        batch.collected_count = 0;
        batch.in_flight = true;
        this->submit_batch_index = (index + 1) % render::MAX_FRAMES_IN_FLIGHT;
        return true;
    }
    return false;
}

U32
TileHeightSampler::collect(Buffer<TileHeightResult> results)
{
    if (results.size == 0)
        return 0;
    Arena* arena = this->allocator->arena;
    ScratchScope scratch = ScratchScope(&arena, 1);
    U32 capacity = (U32)Min(results.size, this->query_capacity);
    Buffer<shader::TileHeightGpuResult> packed = buffer_alloc<shader::TileHeightGpuResult>(scratch.arena, capacity);
    U32 written = 0;
    U32 start = this->collect_batch_index;
    for (U32 offset = 0; offset < render::MAX_FRAMES_IN_FLIGHT && written < results.size; ++offset)
    {
        U32 index = (start + offset) % render::MAX_FRAMES_IN_FLIGHT;
        Batch& batch = this->batches[index];
        if (!batch.in_flight)
            continue;
        U32 count = render::tile_height_batch_collect(batch.handle,
            {packed.data, Min((U64)capacity, results.size - written)});
        for (U32 item = 0; item < count; ++item)
        {
            results.data[written++] = {batch.request_ids.data[batch.collected_count + item],
                                      packed.data[item].hit_distance, (B32)packed.data[item].hit,
                                      {packed.data[item].entry_handle_idx, packed.data[item].entry_handle_gen_id}};
        }
        batch.collected_count += count;
        if (batch.collected_count == batch.query_count)
        {
            batch.in_flight = false;
            this->collect_batch_index = (index + 1) % render::MAX_FRAMES_IN_FLIGHT;
        }
    }
    return written;
}

} // namespace city
