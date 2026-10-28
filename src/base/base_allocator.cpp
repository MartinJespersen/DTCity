#include "diagnostics.hpp"
#include "base/base_inc.hpp"

Allocator
Allocator::create(ArenaParams arena_params) noexcept
{
    Allocator allocator = Allocator(arena_params);
    return allocator;
}

Allocator::Allocator(ArenaParams arena_params)
{
    Arena* arena_input = arena_alloc(&arena_params);
    this->init(arena_input);
}

Allocator::~Allocator()
{
    _allocator_release();
}

Allocator::Allocator(Allocator&& other) noexcept
{
    *this = std::move(other);
}

Allocator&
Allocator::operator=(Allocator&& other) noexcept
{
    if (this != &other)
    {
        _allocator_release();
        this->arena = std::exchange(other.arena, nullptr);
        this->base_pos = std::exchange(other.base_pos, 0);
    }
    return *this;
}

void
Allocator::init(Arena* arena_input)
{
    this->arena = arena_input;
    for (Arena* block = arena_input->current; block; block = block->prev)
    {
        block->destructor_pos = block->res;
        block->destructor_cmt_pos = block->res;
        block->destructor_count = 0;
    }
    this->base_pos = arena_pos(arena_input);
}

// get the # of bytes currently allocated.
U64
Allocator::get_usage()
{
    AssertAlways(this->arena);
    U64 result = arena_pos(this->arena);
    return result;
}

// also some useful popping helpers:
void
Allocator::pop_to(U64 count)
{
    AssertAlways(this->arena);
    if (count <= this->base_pos)
    {
        _allocator_destroy_all();
        count = this->base_pos;
    }
    else
    {
        for (Arena* block = this->arena->current; block; block = block->prev)
            AssertAlways(block->destructor_count == 0);
    }
    arena_pop_to(this->arena, count);
}

void
Allocator::clear()
{
    AssertAlways(this->arena);
    pop_to(0);
}

// push some bytes onto the 'stack' - the way to allocate
void*
Allocator::push(U64 bytes, U64 alignment)
{
    void* result = arena_push(this->arena, bytes, alignment);
    return result;
}

void*
Allocator::push_with_destructor(U64 bytes, U64 alignment, Destructor destructor)
{
    U64 destructor_stride = align_pow2(sizeof(Destructor), Max(8, AlignOf(Destructor)));
    _arena_ensure_capacity(this->arena, bytes, alignment, destructor_stride);
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
    this->arena->current->destructor_count += 1;
}

Destructor*
Allocator::_allocator_push_destructor()
{
    U64 destructor_size = sizeof(Destructor);
    U64 destructor_align = Max(8, AlignOf(Destructor));
    U64 destructor_stride = align_pow2(destructor_size, destructor_align);
    _arena_ensure_capacity(this->arena, 0, 1, destructor_stride);
    Arena* block = this->arena->current;
    AssertAlways(block->destructor_pos >= destructor_stride);

    U64 pos_pst = block->destructor_pos - destructor_stride;
    AssertAlways(block->pos <= pos_pst);

    OS_SystemInfo* system_info = OS_GetSystemInfo();
    U64 page_size = system_info->page_size;
    if (block->flags & ArenaFlag_LargePages)
    {
        page_size = system_info->large_page_size;
    }
    U64 cmt_pos = AlignDownPow2(pos_pst, page_size);
    if (cmt_pos < block->destructor_cmt_pos)
    {
        U64 cmt_size = block->destructor_cmt_pos - cmt_pos;
        U8* cmt_ptr = (U8*)block + cmt_pos;
        B32 committed = block->flags & ArenaFlag_LargePages ? os_commit_large(cmt_ptr, cmt_size)
                                                            : os_commit(cmt_ptr, cmt_size);
        if (Unlikely(!committed))
            _arena_allocation_failure("commit destructor storage", cmt_size);
        block->destructor_cmt_pos = cmt_pos;
#if BUILD_DEBUG
        block->debug_destructor_cmt_pos = Min(block->debug_destructor_cmt_pos, cmt_pos);
#endif
    }

    Destructor* result = (Destructor*)((U8*)block + pos_pst);
    block->destructor_pos = pos_pst;
    AsanUnpoisonMemoryRegion(result, sizeof(*result));
    Debug_ArenaUsage_Push(block);
    return result;
}

void
Allocator::_allocator_destroy_all()
{
    U64 destructor_align = Max(8, AlignOf(Destructor));
    U64 destructor_stride = align_pow2(sizeof(Destructor), destructor_align);
    // Keep every block alive: an older object can own storage in a newer block.
    for (Arena* block = this->arena->current; block; block = block->prev)
    {
        while (block->destructor_count > 0)
        {
            Destructor* destructor = (Destructor*)((U8*)block + block->destructor_pos);
            (*destructor)();
            block->destructor_pos += destructor_stride;
            block->destructor_count -= 1;
        }
        block->destructor_pos = block->res;
        block->destructor_cmt_pos = block->res;
        Debug_ArenaUsage_Push(block);
    }
}

void
Allocator::_allocator_release()
{
    if (this->arena)
    {
        _allocator_destroy_all();
        arena_release(this->arena);
    }
    this->arena = nullptr;
    this->base_pos = 0;
}
