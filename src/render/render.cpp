namespace render
{
////////////////////////////////////////////////////////
// ~mgj: ThreadInput
static render::ThreadWorkerCmdCtx*
thread_input_create()
{
    Arena* arena = arena_alloc();
    Assert(arena);
    render::ThreadWorkerCmdCtx* thread_input = PushStruct(arena, render::ThreadWorkerCmdCtx);
    thread_input->arena = arena;
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
handle_list_push(Arena* arena, render::HandleList* list, render::Handle handle)
{
    Assert(handle.u64);
    render::HandleNode* node = PushStruct(arena, render::HandleNode);
    node->handle = handle;
    SLLQueuePush(list->first, list->last, node);
    list->count++;
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

} // namespace render
