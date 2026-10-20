#pragma once

#include "core_inc.hpp"
#include "asset_manager.hpp"
#include "tile_height_compute.hpp"
#include "base/base_core.hpp"
#include "base/base_strings.hpp"
#include "render/tile_bvh.hpp"
#include <vulkan/vulkan_core.h>
#include "render/tile_height.hpp"

namespace vulkan
{

struct TileHeightBatch
{
    TileHeightBatch* next;
    U32 index;
    U32 generation;
    U32 capacity;
    U32 query_count;
    U32 collected_count;
    B32 in_flight;
    B32 retiring;
    B32 empty;
    render::TileBvhHandle bvh;
    VkCommandPool command_pool;
    VkCommandBuffer command;
    VkFence fence;
    BufferAllocation queries;
    BufferAllocation results;
    BufferReadback readback;
};

void
_tile_height_pipeline_initialize(String8 shader_path);
void
_tile_height_retire_poll();

// Private batch helpers.
B32
_tile_height_batch_find(render::TileHeightBatchHandle handle, TileHeightBatch** batch);
B32
_tile_height_batch_completed(TileHeightBatch* batch);
void
_tile_height_batch_release(TileHeightBatch* batch);

} // namespace vulkan
