#include "cesium/cesium_tileset.hpp"
#include "cesium/cesium_tile_overlay.cpp"
#include <CesiumRasterOverlays/DebugColorizeTilesRasterOverlay.h>
#include <CesiumRasterOverlays/RasterOverlayExternals.h>

TEST_CASE("Tile raster overlay waits for attachment and GPU upload")
{
    // Use the deterministic upload backend shared with the road tests.
    road_renderer = {};
    cesium::TileDrawBatch batch = {};
    batch.render_data.pipeline_bits = render::TilePipelineBits::OverlayEnabled;
    cesium::TileRenderResources tile = {};
    tile.batch_first = &batch;

    CHECK_FALSE(cesium::_tile_render_data_overlay_apply(&tile, true));

    cesium::TileRasterOverlayAttachment attachment = {};
    attachment.texture_handle = render::Handle((void*)1, 1, render::HandleType::Texture);
    attachment.translation = {0.25, 0.125};
    attachment.scale = {0.5, 0.25};
    tile.raster_overlay_first = &attachment;
    CHECK_FALSE(cesium::_tile_render_data_overlay_apply(&tile, true));

    road_renderer.loaded[1] = true;
    REQUIRE(cesium::_tile_render_data_overlay_apply(&tile, true));
    CHECK(batch.render_data.overlay_texture_handle.u64 == 1);
    CHECK(batch.render_data.overlay_texture_coordinate_id == 0);
    CHECK(batch.render_data.overlay_translation.x == doctest::Approx(0.25));
    CHECK(batch.render_data.overlay_translation.y == doctest::Approx(0.875));
    CHECK(batch.render_data.overlay_scale.x == doctest::Approx(0.5));
    CHECK(batch.render_data.overlay_scale.y == doctest::Approx(-0.25));

    // Removing an attachment or losing the upload makes the tile hidden again.
    road_renderer.loaded[1] = false;
    CHECK_FALSE(cesium::_tile_render_data_overlay_apply(&tile, true));
    road_renderer.loaded[1] = true;
    tile.raster_overlay_first = nullptr;
    CHECK_FALSE(cesium::_tile_render_data_overlay_apply(&tile, true));
}

TEST_CASE("Tile raster overlay requires supported UVs on every batch")
{
    road_renderer = {};
    road_renderer.loaded[1] = true;
    cesium::TileRasterOverlayAttachment attachment = {};
    attachment.texture_handle = render::Handle((void*)1, 1, render::HandleType::Texture);
    cesium::TileDrawBatch batches[2] = {};
    batches[0].next = &batches[1];
    batches[0].render_data.pipeline_bits = render::TilePipelineBits::OverlayEnabled;
    cesium::TileRenderResources tile = {};
    tile.batch_first = &batches[0];
    tile.raster_overlay_first = &attachment;

    CHECK_FALSE(cesium::_tile_render_data_overlay_apply(&tile, true));
    batches[1].render_data.pipeline_bits = render::TilePipelineBits::OverlayEnabled;
    REQUIRE(cesium::_tile_render_data_overlay_apply(&tile, true));
    CHECK(batches[1].render_data.overlay_texture_handle.u64 == 1);

    attachment.overlay_texture_coordinate_id = 1;
    CHECK_FALSE(cesium::_tile_render_data_overlay_apply(&tile, true));
    attachment.overlay_texture_coordinate_id = 0;
    attachment.texture_handle = {};
    CHECK_FALSE(cesium::_tile_render_data_overlay_apply(&tile, true));
}

TEST_CASE("Tiles without an enabled raster overlay need no raster image")
{
    road_renderer = {};
    cesium::TileDrawBatch batch = {};
    cesium::TileRenderResources tile = {};
    tile.batch_first = &batch;
    REQUIRE(cesium::_tile_render_data_overlay_apply(&tile, false));
    CHECK(batch.render_data.overlay_texture_handle.u64 == 0);
    CHECK(batch.render_data.overlay_texture_coordinate_id == -1);
    CHECK(batch.render_data.overlay_scale.x == 1.0f);
    CHECK(batch.render_data.overlay_scale.y == 1.0f);

    // Stale UV capability alone must not require an overlay after it is disabled.
    batch.render_data.pipeline_bits = render::TilePipelineBits::OverlayEnabled;
    CHECK(cesium::_tile_render_data_overlay_apply(&tile, false));
}

TEST_CASE("Pending raster refinement retains the parent and suppresses ready siblings")
{
    Allocator owner = Allocator::create();
    Cesium3DTilesSelection::Tile parent(nullptr);
    parent.setRefine(Cesium3DTilesSelection::TileRefine::Replace);
    std::vector<Cesium3DTilesSelection::Tile> children;
    children.emplace_back(nullptr);
    children.emplace_back(nullptr);
    parent.createChildTiles(std::move(children));
    Cesium3DTilesSelection::Tile independent(nullptr);
    cesium::TileRenderResources previous = {};
    previous.retained_tile = &parent;
    cesium::TileDrawSelection selections[3] = {};
    selections[0].tile = &parent.getChildren()[0];
    selections[0].ready = true;
    selections[0].next = &selections[1];
    selections[1].tile = &parent.getChildren()[1];
    selections[1].next = &selections[2];
    selections[2].tile = &independent;
    selections[2].ready = true;
    cesium::TileDrawSelection* first = &selections[0];
    cesium::TileDrawSelection* last = &selections[2];

    // Geometry displayed before imagery was enabled is not a textured fallback.
    cesium::tile_draw_selection_resolve(owner.arena, &first, &last, &previous, true);
    CHECK(last == &selections[2]);
    CHECK_FALSE(selections[0].excluded);

    cesium::tile_draw_selection_resolve(owner.arena, &first, &last, &previous, false);
    CHECK(selections[0].excluded);
    CHECK_FALSE(selections[2].excluded);
    REQUIRE(last->fallback);
    CHECK(last->tile == &parent);
    CHECK(last->render_data == &previous);
    CHECK(last->lod_visibility == 1.0f);
    CHECK(last->next == nullptr);

    // Only the subtree that was pending changes when its final upload completes.
    selections[1].ready = true;
    selections[0].excluded = false;
    selections[2].next = nullptr;
    last = &selections[2];
    cesium::tile_draw_selection_resolve(owner.arena, &first, &last, &previous, false);
    CHECK_FALSE(selections[0].excluded);
    CHECK(last == &selections[2]);

    // Additive content must not be suppressed by a parent fallback.
    parent.setRefine(Cesium3DTilesSelection::TileRefine::Add);
    selections[1].ready = false;
    cesium::tile_draw_selection_resolve(owner.arena, &first, &last, &previous, false);
    CHECK_FALSE(selections[0].excluded);
    CHECK(last == &selections[2]);
}

TEST_CASE("Pending parent raster keeps the previous children after their Cesium fade ends")
{
    Allocator owner = Allocator::create();
    Cesium3DTilesSelection::Tile parent(nullptr);
    parent.setRefine(Cesium3DTilesSelection::TileRefine::Replace);
    std::vector<Cesium3DTilesSelection::Tile> children;
    children.emplace_back(nullptr);
    children.emplace_back(nullptr);
    parent.createChildTiles(std::move(children));
    cesium::TileRenderResources previous[2] = {};
    previous[0].retained_tile = &parent.getChildren()[0];
    previous[0].retained_next = &previous[1];
    previous[1].retained_tile = &parent.getChildren()[1];
    cesium::TileDrawSelection selections[2] = {};
    selections[0].tile = &parent;
    selections[0].next = &selections[1];
    selections[1].tile = &parent.getChildren()[0];
    selections[1].is_fading_out = true;
    selections[1].ready = true;
    selections[1].lod_visibility = 0.0f;
    cesium::TileDrawSelection* first = &selections[0];
    cesium::TileDrawSelection* last = &selections[1];

    cesium::tile_draw_selection_resolve(owner.arena, &first, &last, previous, false);
    CHECK(selections[1].excluded);
    U32 fallback_count = 0;
    for (auto* node = first; node; node = node->next)
    {
        if (node->fallback)
        {
            CHECK_FALSE(node->excluded);
            CHECK(node->lod_visibility == 1.0f);
            fallback_count++;
        }
    }
    CHECK(fallback_count == 2);
}

TEST_CASE("Displayed raster survives detachment until the replacement GPU upload completes")
{
    road_renderer = {};
    Allocator owner = Allocator::create();
    auto* renderer = PushStruct(owner.arena, cesium::TilesetRenderer);
    renderer->draw_selection_frame = 1;
    Cesium3DTilesSelection::Tile geometry(nullptr);
    CesiumRasterOverlays::RasterOverlayExternals externals = {nullptr, nullptr, CesiumAsync::AsyncSystem(nullptr)};
    CesiumUtility::IntrusivePointer<const CesiumRasterOverlays::RasterOverlay> overlay =
        new CesiumRasterOverlays::DebugColorizeTilesRasterOverlay("test raster retention");
    CesiumUtility::IntrusivePointer<CesiumRasterOverlays::ActivatedRasterOverlay> activated =
        new CesiumRasterOverlays::ActivatedRasterOverlay(externals, overlay);
    CesiumUtility::IntrusivePointer<CesiumRasterOverlays::RasterOverlayTile> old_raster =
        new CesiumRasterOverlays::RasterOverlayTile(*activated);
    CesiumUtility::IntrusivePointer<CesiumRasterOverlays::RasterOverlayTile> new_raster =
        new CesiumRasterOverlays::RasterOverlayTile(*activated);
    cesium::TileRasterOverlayAttachment attachment = {};
    attachment.raster_tile = old_raster.get();
    attachment.texture_handle = render::Handle((void*)1, 1, render::HandleType::Texture);
    attachment.scale = {0.5, 0.25};
    road_renderer.loaded[1] = true;
    cesium::TileDrawBatch batch = {};
    batch.render_data.pipeline_bits = render::TilePipelineBits::OverlayEnabled;
    cesium::TileRenderResources data = {};
    data.batch_first = &batch;
    data.raster_overlay_first = &attachment;
    cesium::TileDrawSelection selection = {};
    selection.tile = &geometry;
    selection.render_data = &data;
    selection.lod_visibility = 1.0f;
    selection.ready = cesium::_tile_render_data_overlay_apply(&data, true);
    REQUIRE(selection.ready);
    cesium::tile_draw_selection_commit(renderer, &selection);
    defer(cesium::tileset_renderer_retained_tiles_prune(renderer, 0));
    CHECK(geometry.getReferenceCount() == 1);
    CHECK(old_raster->getReferenceCount() == 2);
    CHECK(activated->getReferenceCount() == 2);

    cesium::tile_draw_selection_commit(renderer, &selection);
    CHECK(renderer->tiles_to_show_count == 1);
    CHECK(data.render_next == nullptr);

    // Cesium has detached the old raster; its mapping remains drawable.
    data.raster_overlay_first = nullptr;
    REQUIRE(cesium::_tile_render_data_overlay_apply(&data, true));
    CHECK(batch.render_data.overlay_texture_handle.u64 == 1);
    attachment.raster_tile = new_raster.get();
    attachment.texture_handle = render::Handle((void*)2, 1, render::HandleType::Texture);
    attachment.scale = {1.0, 1.0};
    data.raster_overlay_first = &attachment;
    REQUIRE(cesium::_tile_render_data_overlay_apply(&data, true));
    CHECK(batch.render_data.overlay_texture_handle.u64 == 1);
    CHECK(batch.render_data.overlay_scale.x == 0.5f);
    CHECK(old_raster->getReferenceCount() == 2);

    cesium::TileDrawSelection pending = {};
    pending.tile = &geometry;
    cesium::TileDrawSelection* first = &pending;
    cesium::TileDrawSelection* last = &pending;
    cesium::tile_draw_selection_resolve(owner.arena, &first, &last, &data, true);
    REQUIRE(last->fallback);
    CHECK(last->render_data == &data);
    renderer->draw_selection_frame++;
    renderer->tile_to_show_first = nullptr;
    renderer->tile_to_show_last = nullptr;
    renderer->tiles_to_show_count = 0;
    cesium::tile_draw_selection_commit(renderer, last);
    cesium::tileset_renderer_retained_tiles_prune(renderer, renderer->draw_selection_frame);
    CHECK(renderer->tiles_to_show_count == 1);
    CHECK(batch.render_data.lod_fade == 1.0f);
    CHECK(geometry.getReferenceCount() == 1);

    road_renderer.loaded[2] = true;
    REQUIRE(cesium::_tile_render_data_overlay_apply(&data, true));
    CHECK(batch.render_data.overlay_texture_handle.u64 == 2);
    CHECK(batch.render_data.overlay_scale.x == 1.0f);
    CHECK(old_raster->getReferenceCount() == 1);
    CHECK(new_raster->getReferenceCount() == 2);
    CHECK(activated->getReferenceCount() == 2);

    // Leaving the draw list releases both geometry and the activated overlay.
    cesium::tileset_renderer_retained_tiles_prune(renderer, 3);
    CHECK(geometry.getReferenceCount() == 0);
    CHECK(new_raster->getReferenceCount() == 1);
    CHECK(activated->getReferenceCount() == 1);
    CHECK(renderer->retained_tile_first == nullptr);
}
