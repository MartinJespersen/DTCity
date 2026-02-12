namespace render
{
////////////////////////////////////////////////////////
// ~mgj: ThreadInput
static render::ThreadInput*
thread_input_create()
{
    Arena* arena = arena_alloc();
    Assert(arena);
    render::ThreadInput* thread_input = PushStruct(arena, render::ThreadInput);
    thread_input->arena = arena;
    return thread_input;
}

static void
thread_input_destroy(render::ThreadInput* thread_input)
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
static render::BufferInfo
buffer_info_from_vertex_3d_buffer(Buffer<Vertex3D> buffer, render::BufferType buffer_type)
{
    U64 type_size = sizeof(Vertex3D);
    return render::buffer_info_from_template_buffer(buffer, buffer_type, type_size);
}

static render::BufferInfo
buffer_info_from_vertex_blend_3d_buffer(Buffer<Vertex3DBlend> buffer,
                                        render::BufferType buffer_type)
{
    U64 type_size = sizeof(Vertex3DBlend);
    return render::buffer_info_from_template_buffer(buffer, buffer_type, type_size);
}

static render::BufferInfo
buffer_info_from_u32_index_buffer(Buffer<U32> buffer, render::BufferType buffer_type)
{
    U64 type_size = sizeof(U32);
    return render::buffer_info_from_template_buffer(buffer, buffer_type, type_size);
}

static render::BufferInfo
buffer_info_from_vertex_3d_instance_buffer(Buffer<render::Model3DInstance> buffer,
                                           render::BufferType buffer_type)
{
    U64 type_size = sizeof(render::Model3DInstance);
    return render::buffer_info_from_template_buffer(buffer, buffer_type, type_size);
}

static bool
is_handle_zero(render::Handle handle)
{
    return handle.ptr == 0;
}

static void
handle_list_push(Arena* arena, render::HandleList* list, render::Handle handle)
{
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
static render::BufferInfo
buffer_info_from_template_buffer(Buffer<T> buffer, render::BufferType buffer_type, U64 type_size)
{
    U64 byte_count = buffer.size * type_size;
    Buffer<U8> general_buffer = {.data = (U8*)buffer.data, .size = byte_count};
    return {.buffer = general_buffer, .type_size = type_size, .buffer_type = buffer_type};
}

} // namespace render
