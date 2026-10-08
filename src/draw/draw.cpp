#include "core_inc.hpp"
#include "base/base_container.hpp"
#include "base/base_container_templates.hpp"
#include "render/render.hpp"
#include "render/render_templates.hpp"
#include "misc/camera.hpp"
#include "draw/draw.hpp"
#include "entrypoint.hpp"

namespace draw
{

g_internal Draw g_draw_ctx = {};

void
draw_init()
{
    if (!g_draw_ctx.frame_arena)
    {
        g_draw_ctx.frame_arena = arena_alloc();
        Debug_SetName(g_draw_ctx.frame_arena, "draw frame arena");
    }
}

void
draw_release()
{
    if (g_draw_ctx.frame_arena)
    {
        arena_release(g_draw_ctx.frame_arena);
    }
    g_draw_ctx = {};
}

void
draw_new_frame()
{
    arena_clear(g_draw_ctx.frame_arena);
    g_draw_ctx.frame = PushStruct(g_draw_ctx.frame_arena, DrawFrame);
    g_draw_ctx.frame->line_vertex_chunk_list = chunk_list_create<render::LineVertex>(g_draw_ctx.frame_arena, 200);
    g_draw_ctx.frame->mesh_instance_batches = chunk_list_create<render::MeshInstanceBatch>(g_draw_ctx.frame_arena, 32);
}

Arena*
draw_frame_arena_get()
{
    return g_draw_ctx.frame_arena;
}

DrawFrame*
draw_frame_get()
{
    if (!g_draw_ctx.frame)
    {
        draw_new_frame();
    }
    return g_draw_ctx.frame;
}

void
draw_camera_set(ArrayResourcePoolHandle camera_resource_handle)
{
    Context* ctx = dt_ctx_get();
    DrawFrame* frame = draw_frame_get();
    ui::Camera* camera = {};
    if (ctx->camera_container->item_from_handle(camera_resource_handle, &camera))
    {
        frame->camera_resource_handle = camera_resource_handle;
    }
}

void
draw_line(render::Line& line)
{
    DrawFrame* frame = draw_frame_get();
    render::LineVertex from = {.pos = line.from, .color = line.color};
    render::LineVertex to = {.pos = line.to, .color = line.color};
    chunk_list_insert(g_draw_ctx.frame_arena, frame->line_vertex_chunk_list, from);
    chunk_list_insert(g_draw_ctx.frame_arena, frame->line_vertex_chunk_list, to);
}

void
primitive_draw(glm::vec3 location, F32 scale_factor, render::MeshHandle mesh_handle)
{
    DrawFrame* frame = draw_frame_get();
    PrimitiveInstanceNode* matching_instance = 0;
    for (PrimitiveInstanceNode* instance = frame->primitive_instance_list.first; instance; instance = instance->next)
    {
        render::Handle* instance_vertex_handle = &instance->mesh_handle.vertex_buffer_handle;
        render::Handle* vertex_handle = &mesh_handle.vertex_buffer_handle;
        render::Handle* instance_index_handle = &instance->mesh_handle.index_buffer_handle;
        render::Handle* index_handle = &mesh_handle.index_buffer_handle;
        B32 vertex_handle_matches = instance_vertex_handle->ptr == vertex_handle->ptr &&
                                    instance_vertex_handle->gen_id == vertex_handle->gen_id &&
                                    instance_vertex_handle->type == vertex_handle->type;
        B32 index_handle_matches = instance_index_handle->ptr == index_handle->ptr &&
                                   instance_index_handle->gen_id == index_handle->gen_id &&
                                   instance_index_handle->type == index_handle->type;
        B32 mesh_matches = vertex_handle_matches && index_handle_matches;
        if (mesh_matches && instance->scale_factor == scale_factor)
        {
            matching_instance = instance;
            break;
        }
    }

    if (!matching_instance)
    {
        matching_instance = PushStruct(g_draw_ctx.frame_arena, PrimitiveInstanceNode);
        matching_instance->location_chunk_list = chunk_list_create<glm::vec3>(g_draw_ctx.frame_arena, 100);
        matching_instance->scale_factor = scale_factor;
        matching_instance->mesh_handle = mesh_handle;
        SLLQueuePush(frame->primitive_instance_list.first, frame->primitive_instance_list.last, matching_instance);
        frame->primitive_instance_list.count++;
    }

    chunk_list_insert(g_draw_ctx.frame_arena, matching_instance->location_chunk_list, location);
}

g_internal void
draw_mesh_instances(render::MeshletMeshHandle mesh_handle, Buffer<render::Transform> transforms)
{
    if (transforms.size == 0)
    {
        return;
    }

    DrawFrame* frame = draw_frame_get();
    Buffer<render::Transform> frame_transforms = buffer_arena_copy(g_draw_ctx.frame_arena, transforms);
    render::MeshInstanceBatch batch = {.transforms = frame_transforms, .mesh_handle = mesh_handle};
    chunk_list_insert(g_draw_ctx.frame_arena, frame->mesh_instance_batches, batch);
}

} // namespace draw
