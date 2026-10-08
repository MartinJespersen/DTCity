#include "core_inc.hpp"
#include "base/base_container.hpp"
#include "base/base_container_templates.hpp"
#include "base/base_lists.hpp"
#include "base/base_lists_templates.hpp"
#include "async/thread_pool.hpp"
#include "render/render_inc.hpp"
#include "draw/draw.hpp"
#include "misc/io.hpp"
#include "entrypoint.hpp"

namespace render
{
////////////////////////////////////////////////////////
// ~mgj: ThreadInput
render::ThreadWorkerCmdCtx*
thread_ctx_create()
{
    Context* ctx = dt_ctx_get();
    Arena* arena = arena_alloc();
    Debug_SetName(arena, "render thread command arena");
    Assert(arena);
    render::ThreadWorkerCmdCtx* thread_input = PushStruct(arena, render::ThreadWorkerCmdCtx);
    thread_input->arena = arena;
    thread_input->thread_pool = ctx->thread_pool;

    return thread_input;
}

void
thread_input_destroy(render::ThreadWorkerCmdCtx* thread_input)
{
    arena_release(thread_input->arena);
}

////////////////////////////////////////////////////////
// ~mgj: Handles
render::Handle
handle_zero()
{
    render::Handle handle = render::Handle(nullptr, 0, render::HandleType::Undefined);
    return handle;
}

////////////////////////////////////////////////////////

bool
is_handle_zero(render::Handle handle)
{
    return handle.ptr == 0;
}

void
handle_list_push(ThreadWorkerCmdCtx* thread_ctx, render::Handle handle)
{
    Assert(handle.u64);
    render::HandleNode* node = PushStruct(thread_ctx->arena, render::HandleNode);
    node->handle = handle;
    SLLQueuePush(thread_ctx->handles.first, thread_ctx->handles.last, node);
    thread_ctx->handles.count++;
}

render::Handle
handle_list_first_handle(render::HandleList* list)
{
    Assert(list->first);
    return list->first->handle;
}

// privates

BufferInfo
BufferInfo::copy_to_arena(Arena* arena)
{
    BufferInfo buffer_info = *this;
    buffer_info.buffer = buffer_alloc<U8>(arena, this->type_size * this->elem_count);
    return buffer_info;
}

} // namespace render
