#pragma once

#include "base/base_core.hpp"
#include "base/container.hpp"
#include "render/tile_bvh.hpp"
#include "shaders/shader_shared.h"

namespace render
{

struct TileHeightBatchHandle
{
    U32 idx;
    U32 gen_id;
};

// Owner-thread API. Each batch owns reusable upload/result/readback storage.
TileHeightBatchHandle
tile_height_batch_create(U32 capacity);
B32
tile_height_batch_submit(TileHeightBatchHandle batch, TileBvhHandle bvh,
                         Buffer<const shader::TileHeightGpuQuery> queries);
// Copies results only after GPU completion. Partial collection keeps the batch occupied.
U32
tile_height_batch_collect(TileHeightBatchHandle batch, Buffer<shader::TileHeightGpuResult> results);
// In-flight work is retired by gpu_work_update(); never waits for the GPU.
void
tile_height_batch_destroy(TileHeightBatchHandle batch);

} // namespace render
