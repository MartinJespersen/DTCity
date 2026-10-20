#pragma once

#include "core_inc.hpp"
#include "base/base_core.hpp"
#include "base/base_math.h"
#include "async/thread_pool.hpp"
#include "base/container.hpp"
#include "render/render.hpp"
#include "render/tile_bvh.hpp"

namespace vulkan
{

struct TileBvhResource
{
    TileBvhResource* next;
    TileBvhResource* next_free;
    U32 index;
    U32 generation;
    U32 reference_count;
    B32 ready;
    render::TileBvhResourceView view;
    Array<render::TileBvhMeshBuffers> meshes;
};

struct TileBvhUpload
{
    TileBvhResource* resource;
    Buffer<Rng3F32> bounds;
    Buffer<render::TileBvhGpuEntry> entries;
};

// Private worker and registry helpers.
B32
_tile_bvh_resource_find(render::TileBvhHandle handle, TileBvhResource** resource);
async::WorkerResult
_tile_bvh_build_worker(async::ThreadInfo info, async::WorkerData data);
void
_tile_bvh_upload_complete(render::ThreadWorkerCmdCtx* thread_ctx);

} // namespace vulkan
