#ifndef BASE_ALLOCATOR_HPP
#define BASE_ALLOCATOR_HPP

struct Destructor
{
    void const* bound_object;
    void (*destroy)(void const*);

    template <typename T>
    static Destructor
    create(T const* object)
    {
        Destructor result = {};
        result.bound_object = object;
        result.destroy = +[](void const* x) { static_cast<T const*>(x)->~T(); };
        return result;
    }

    void
    operator()() const
    {
        destroy(bound_object);
    }
};

struct Allocator
{
  private:
    U64 destructor_cmt_pos;
    U64 destructor_count;
    U64 base_pos;

  public:
    Arena* arena;

    Allocator() = default;

    ~Allocator();

    static Allocator*
    create(ArenaParams arena_params = {arena_default_reserve_size, arena_default_commit_size, arena_default_flags}) noexcept;

    static void
    destroy(Allocator* allocator)
    {
        allocator->~Allocator();
    }

    // get the # of bytes currently allocated.
    U64
    get_usage();

    // also some useful popping helpers:
    void
    pop_to(U64 count);
    void
    clear();

    template <typename T, typename... Args>
    T*
    place(Args&&... args)
    {
        void* mem = nullptr;
        if constexpr (std::is_trivially_destructible_v<T>)
        {
            mem = push(sizeof(T), alignof(T));
        }
        else
        {
            mem = push_with_destructor(sizeof(T), alignof(T), Destructor::create<T>(nullptr));
        }
        return static_cast<T*>(new (mem) T{std::forward<Args>(args)...});
    }

    template <typename T, typename... Args>
    void
    place(T* mem, Args&&... args)
    {
        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            _push_only_destructor(mem, Destructor::create<T>(nullptr));
        }
        new (mem) T{std::forward<Args>(args)...};
    }

  private:
    // push some bytes onto the 'stack' - the way to allocate
    void*
    push(U64 bytes, U64 alignment = 8);
    void*
    push_with_destructor(U64 bytes, U64 alignment, Destructor destructor);
    Destructor*
    _allocator_push_destructor();
    void
    _allocator_destroy_all();
    void
    _push_only_destructor(void* object, Destructor destructor);
};

#endif // BASE_ALLOCATOR_HPP
