Allocator*
Allocator::create(ArenaParams arena_params) noexcept
{
    arena_params.flags |= ArenaFlag_NoChain;
    Arena* arena = arena_alloc(&arena_params);
    Allocator* allocator = PushStruct(arena, Allocator);
    new (allocator) Allocator{};

    allocator->arena = arena;
    arena->destructor_pos = arena->res;
    allocator->destructor_cmt_pos = arena->res;
    allocator->destructor_count = 0;
    allocator->base_pos = arena_pos(arena);
    return allocator;
}

Allocator::~Allocator()
{
    _allocator_destroy_all();
    arena_release(arena);
}

// push some bytes onto the 'stack' - the way to allocate
void*
Allocator::push(U64 bytes, U64 alignment)
{
    U64 pos_pre = align_pow2(arena->pos, alignment);
    U64 pos_pst = pos_pre + bytes;
    AssertAlways(pos_pst <= arena->destructor_pos);

    void* result = arena_push(arena, bytes, alignment);
    return result;
}

void*
Allocator::push_with_destructor(U64 bytes, U64 alignment, Destructor destructor)
{
    void* result = push(bytes, alignment);

    _push_only_destructor(result, destructor);

    return result;
}

void
Allocator::_push_only_destructor(void* object, Destructor destructor)
{
    Destructor* destructor_slot = _allocator_push_destructor();
    destructor.bound_object = object;
    *destructor_slot = destructor;
    destructor_count += 1;
}
// get the # of bytes currently allocated.
U64
Allocator::get_usage()
{
    U64 result = arena_pos(arena);
    return result;
}

// also some useful popping helpers:
void
Allocator::pop_to(U64 count)
{
    if (count <= base_pos)
    {
        _allocator_destroy_all();
        count = base_pos;
    }
    else
    {
        AssertAlways(destructor_count == 0);
    }
    arena_pop_to(arena, count);
}

void
Allocator::clear()
{
    pop_to(0);
}

Destructor*
Allocator::_allocator_push_destructor()
{
    U64 destructor_size = sizeof(Destructor);
    U64 destructor_align = Max(8, AlignOf(Destructor));
    U64 destructor_stride = align_pow2(destructor_size, destructor_align);
    AssertAlways(arena->destructor_pos >= destructor_stride);

    U64 pos_pst = arena->destructor_pos - destructor_stride;
    AssertAlways(arena->pos <= pos_pst);

    OS_SystemInfo* system_info = OS_GetSystemInfo();
    U64 page_size = system_info->page_size;
    if (arena->flags & ArenaFlag_LargePages)
    {
        page_size = system_info->large_page_size;
    }
    U64 cmt_pos = AlignDownPow2(pos_pst, page_size);
    if (cmt_pos < destructor_cmt_pos)
    {
        U64 cmt_size = destructor_cmt_pos - cmt_pos;
        U8* cmt_ptr = (U8*)arena + cmt_pos;
        if (arena->flags & ArenaFlag_LargePages)
        {
            os_commit_large(cmt_ptr, cmt_size);
        }
        else
        {
            os_commit(cmt_ptr, cmt_size);
        }
        destructor_cmt_pos = cmt_pos;
    }

    Destructor* result = (Destructor*)((U8*)arena + pos_pst);
    arena->destructor_pos = pos_pst;
    AsanUnpoisonMemoryRegion(result, sizeof(*result));
    return result;
}

void
Allocator::_allocator_destroy_all()
{
    U64 destructor_align = Max(8, AlignOf(Destructor));
    U64 destructor_stride = align_pow2(sizeof(Destructor), destructor_align);
    while (destructor_count > 0)
    {
        Destructor* destructor = (Destructor*)((U8*)arena + arena->destructor_pos);
        (*destructor)();
        arena->destructor_pos += destructor_stride;
        destructor_count -= 1;
    }
    arena->destructor_pos = arena->res;
}
