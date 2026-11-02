#include "core_inc.hpp"
#include "cesium_tileset.hpp"
#include <CesiumRasterOverlays/ActivatedRasterOverlay.h>

namespace cesium
{

g_internal void
_tile_render_data_raster_retain(TileRenderResources* data,
                                const CesiumRasterOverlays::RasterOverlayTile* raster);

g_internal B32
_tile_render_data_overlay_apply(TileRenderResources* render_data_list, B32 raster_overlay_enabled);

g_internal TileRasterOverlayAttachment*
_tile_active_raster_attachment_get(TileRenderResources* render_data_list);

g_internal B32
_tile_replacement_contains(const Cesium3DTilesSelection::Tile* ancestor,
                           const Cesium3DTilesSelection::Tile* descendant);

void
tile_draw_selection_push(Arena* arena, TileDrawSelection** first, TileDrawSelection** last,
                          const Cesium3DTilesSelection::Tile& tile, B32 is_fading_out,
                          B32 raster_overlay_enabled)
{
    if (tile.getState() != Cesium3DTilesSelection::TileLoadState::Done)
        return;
    const Cesium3DTilesSelection::TileRenderContent* content = tile.getContent().getRenderContent();
    if (!content || !content->getRenderResources())
        return;

    TileDrawSelection* selection = PushStruct(arena, TileDrawSelection);
    selection->tile = &tile;
    selection->render_data = static_cast<TileRenderResources*>(content->getRenderResources());
    selection->is_fading_out = is_fading_out;
    F32 lod_fade = glm::clamp(content->getLodTransitionFadePercentage(), 0.0f, 1.0f);
    selection->lod_visibility = is_fading_out ? 1.0f - lod_fade : lod_fade;
    selection->ready = _tile_render_data_overlay_apply(selection->render_data, raster_overlay_enabled);
    SLLQueuePush(*first, *last, selection);
}

void
tile_draw_selection_resolve(Arena* arena, TileDrawSelection** first, TileDrawSelection** last,
                             TileRenderResources* retained_first, B32 raster_overlay_enabled)
{
    TileDrawSelection* fallback_first = nullptr;
    // Keep the previous coverage of a replacement subtree until every selected
    // tile in that subtree is ready. This also covers zooming out to a parent.
    for (TileDrawSelection* pending = *first; pending && !pending->fallback; pending = pending->next)
    {
        if (pending->ready || pending->is_fading_out || pending->fallback)
            continue;
        for (TileRenderResources* retained = retained_first; retained; retained = retained->retained_next)
        {
            if (raster_overlay_enabled && (!retained->retained_raster_tile || !retained->batch_first ||
                !render::is_resource_loaded(retained->batch_first->render_data.overlay_texture_handle)))
                continue;
            if (_tile_replacement_contains(retained->retained_tile, pending->tile) ||
                _tile_replacement_contains(pending->tile, retained->retained_tile))
            {
                B32 already_added = false;
                for (TileDrawSelection* node = fallback_first; node; node = node->next)
                    already_added |= node->tile == retained->retained_tile;
                if (already_added)
                    continue;
                TileDrawSelection* fallback = PushStruct(arena, TileDrawSelection);
                fallback->tile = retained->retained_tile;
                fallback->render_data = retained;
                fallback->lod_visibility = 1.0f;
                fallback->ready = true;
                fallback->fallback = true;
                SLLQueuePush(*first, *last, fallback);
                if (!fallback_first)
                    fallback_first = fallback;
            }
        }
    }

    if (!fallback_first)
        return;
    // Do not draw a ready child over a retained parent, or a new parent over
    // retained children. Independent subtrees can still advance their LOD.
    for (TileDrawSelection* selection = *first; selection; selection = selection->next)
    {
        if (!selection->ready)
            continue;
        for (TileDrawSelection* fallback = fallback_first; fallback; fallback = fallback->next)
        {
            if (fallback == selection)
                continue;
            B32 covered = _tile_replacement_contains(fallback->tile, selection->tile);
            B32 covers = _tile_replacement_contains(selection->tile, fallback->tile);
            if ((covered && (fallback->tile != selection->tile || !selection->fallback)) ||
                (covers && !selection->fallback))
            {
                selection->excluded = true;
                break;
            }
        }
    }
}

void
tile_draw_selection_commit(TilesetRenderer* renderer, TileDrawSelection* selection)
{
    TileRenderResources* data = selection->render_data;
    if (!selection->ready || selection->excluded || selection->lod_visibility <= 0.0f ||
        data->draw_selection_frame == renderer->draw_selection_frame)
        return;

    if (!data->retained_tile)
    {
        data->retained_tile = selection->tile;
        data->retained_tile->addReference("DTCity displayed tile");
        _tile_render_data_raster_retain(data, data->applied_raster_tile);
        SLLStackPush_N(renderer->retained_tile_first, data, retained_next);
    }
    data->draw_selection_frame = renderer->draw_selection_frame;
    for (TileDrawBatch* batch = data->batch_first; batch; batch = batch->next)
        batch->render_data.lod_fade = selection->lod_visibility;
    SLLQueuePush_N(renderer->tile_to_show_first, renderer->tile_to_show_last, data, render_next);
    renderer->tiles_to_show_count++;
}

void
tileset_renderer_retained_tiles_prune(TilesetRenderer* renderer, U64 draw_frame)
{
    TileRenderResources** previous_next = &renderer->retained_tile_first;
    for (TileRenderResources* data = renderer->retained_tile_first; data;)
    {
        TileRenderResources* next = data->retained_next;
        // Frame zero is reserved for teardown, when all retained references must go.
        if (draw_frame == 0 || data->draw_selection_frame != draw_frame)
        {
            *previous_next = next;
            const Cesium3DTilesSelection::Tile* tile = data->retained_tile;
            data->retained_next = nullptr;
            data->retained_tile = nullptr;
            data->applied_raster_tile = nullptr;
            _tile_render_data_raster_retain(data, nullptr);
            tile->releaseReference("DTCity displayed tile");
        }
        else
        {
            previous_next = &data->retained_next;
        }
        data = next;
    }
}

g_internal TileRasterOverlayAttachment*
_tile_active_raster_attachment_get(TileRenderResources* render_data_list)
{
    if (!render_data_list)
    {
        return nullptr;
    }

    TileRasterOverlayAttachment* result = nullptr;

    for (TileRasterOverlayAttachment* attachment = render_data_list->raster_overlay_first; attachment;
         attachment = attachment->next)
    {
        if (attachment->overlay_texture_coordinate_id == 0 &&
            render::is_handle_zero(attachment->texture_handle) == false)
        {
            result = attachment;
            break;
        }
    }

    return result;
}

g_internal B32
_tile_render_data_overlay_apply(TileRenderResources* render_data_list, B32 raster_overlay_enabled)
{
    if (!render_data_list)
        return !raster_overlay_enabled;
    TileRasterOverlayAttachment* attachment = _tile_active_raster_attachment_get(render_data_list);

    // An enabled overlay must be attached and uploaded before any part of the tile is shown.
    if (raster_overlay_enabled && (!attachment || !render::is_resource_loaded(attachment->texture_handle)))
    {
        // Cesium can detach the old raster before the new upload completes. Its
        // retained reference keeps the previously applied UV mapping and texture valid.
        return render_data_list->retained_raster_tile && render_data_list->batch_first &&
            render::is_resource_loaded(render_data_list->batch_first->render_data.overlay_texture_handle);
    }

    for (TileDrawBatch* render_data = render_data_list->batch_first; render_data;
         render_data = render_data->next)
    {
        if (raster_overlay_enabled &&
            !has_flag(render_data->render_data.pipeline_bits, render::TilePipelineBits::OverlayEnabled))
        {
            return false;
        }

        render_data->render_data.overlay_texture_handle = {};
        render_data->render_data.overlay_translation = {};
        render_data->render_data.overlay_scale = {1.0f, 1.0f};
        render_data->render_data.overlay_texture_coordinate_id = -1;

        if (attachment)
        {
            render_data->render_data.overlay_texture_handle = attachment->texture_handle;
            render_data->render_data.overlay_translation.x = (F32)attachment->translation.x;
            // Cesium overlay V is south-to-north. Uploaded raster rows are north-to-south.
            render_data->render_data.overlay_translation.y = (F32)(1.0 - attachment->translation.y);
            render_data->render_data.overlay_scale.x = (F32)attachment->scale.x;
            render_data->render_data.overlay_scale.y = (F32)(-attachment->scale.y);
            render_data->render_data.overlay_texture_coordinate_id = attachment->overlay_texture_coordinate_id;
        }
    }
    render_data_list->applied_raster_tile = attachment ? attachment->raster_tile : nullptr;
    if (render_data_list->retained_tile)
        _tile_render_data_raster_retain(render_data_list, render_data_list->applied_raster_tile);
    return true;
}

g_internal B32
_tile_replacement_contains(const Cesium3DTilesSelection::Tile* ancestor,
                           const Cesium3DTilesSelection::Tile* descendant)
{
    for (const Cesium3DTilesSelection::Tile* tile = descendant; tile;)
    {
        if (tile == ancestor)
            return true;
        tile = tile->getParent();
        if (tile && tile->getRefine() != Cesium3DTilesSelection::TileRefine::Replace)
            break;
    }
    return false;
}

g_internal void
_tile_render_data_raster_retain(TileRenderResources* data,
                                const CesiumRasterOverlays::RasterOverlayTile* raster)
{
    const CesiumRasterOverlays::RasterOverlayTile* previous = data->retained_raster_tile;
    if (previous == raster)
        return;
    // Raster tiles hold a raw pointer to their activated overlay. Retain both
    // so removing an overlay cannot destroy the provider behind a displayed tile.
    if (raster)
    {
        raster->getActivatedOverlay().addReference();
        raster->addReference();
    }
    data->retained_raster_tile = raster;
    if (previous)
    {
        const CesiumRasterOverlays::ActivatedRasterOverlay* overlay = &previous->getActivatedOverlay();
        previous->releaseReference();
        overlay->releaseReference();
    }
}

} // namespace cesium
