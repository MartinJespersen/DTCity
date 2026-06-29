namespace render
{
////////////////////////////////////////////////////////
// ~mgj: ThreadInput
static render::ThreadWorkerCmdCtx*
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

static void
thread_input_destroy(render::ThreadWorkerCmdCtx* thread_input)
{
    arena_release(thread_input->arena);
}

////////////////////////////////////////////////////////
// ~mgj: Handles
static render::Handle
handle_zero()
{
    render::Handle handle = render::Handle(nullptr, 0, render::HandleType::Undefined);
    return handle;
}

////////////////////////////////////////////////////////

static bool
is_handle_zero(render::Handle handle)
{
    return handle.ptr == 0;
}

static void
handle_list_push(ThreadWorkerCmdCtx* thread_ctx, render::Handle handle)
{
    Assert(handle.u64);
    render::HandleNode* node = PushStruct(thread_ctx->arena, render::HandleNode);
    node->handle = handle;
    SLLQueuePush(thread_ctx->handles.first, thread_ctx->handles.last, node);
    thread_ctx->handles.count++;
}

static render::Handle
handle_list_first_handle(render::HandleList* list)
{
    Assert(list->first);
    return list->first->handle;
}

// privates
template <typename T>
render::BufferInfo::BufferInfo(Buffer<T> buffer, U32 buffer_type)
{
    U64 byte_count = buffer.size * sizeof(T);
    Buffer<U8> general_buffer = {.data = (U8*)buffer.data, .size = byte_count};
    this->buffer = general_buffer;
    this->type_size = sizeof(T);
    this->buffer_type = buffer_type;
    this->elem_count = buffer.size;
}

template <typename T>
render::BufferInfo::BufferInfo(Arena* arena, T* input, U32 buffer_type)
{
    T* node = PushStruct(arena, T);
    *node = *input;
    Buffer<U8> general_buffer = {.data = (U8*)node, .size = sizeof(T)};
    this->buffer = general_buffer;
    this->type_size = sizeof(T);
    this->buffer_type = buffer_type;
    this->elem_count = 1;
}

BufferInfo
BufferInfo::copy_to_arena(Arena* arena)
{
    BufferInfo buffer_info = *this;
    buffer_info.buffer = buffer_alloc<U8>(arena, this->type_size * this->elem_count);
    return buffer_info;
}

template <typename T>
render::BufferInfo
render::BufferInfo::empty_buffer_info(Arena* arena, BufferType buffer_type)
{
    T* node = PushStruct(arena, T);
    Buffer<U8> buffer = {.data = (U8*)node, .size = sizeof(T)};
    render::BufferInfo buffer_info = render::BufferInfo(buffer, buffer_type);
    return buffer_info;
}

} // namespace render
