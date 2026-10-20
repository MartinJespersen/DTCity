#pragma once

#include "base/base_core.hpp"
#include "base/container.hpp"
#include "render/tile_bvh.hpp"
#include "render/tile_height.hpp"

namespace tile_bvh_test
{

struct Resource
{
    Array<render::TileBvhMeshBuffers> meshes;
    Array<render::TileBvhGpuEntry> entries;
    render::TileBvhResourceView view;
    U32 references;
    B32 ready;
};

struct Backend
{
    Resource resources[64];
    U32 buffer_references[256];
    U32 created;
    B32 reject_submission;
};

struct HeightBatch
{
    Array<shader::TileHeightGpuQuery> queries;
    Array<shader::TileHeightGpuResult> results;
    render::TileBvhHandle bvh;
    U32 count;
    U32 collected;
    B32 live;
    B32 ready;
    B32 retiring;
};

extern Backend backend;
extern HeightBatch height_batches[32];
extern U32 height_batch_count;
void
tile_height_test_complete(U32 index);

} // namespace tile_bvh_test
