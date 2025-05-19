#pragma once

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
struct u128
{
    u64 data[2];
};

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float f32;
typedef double f64;

typedef uint32_t b32;
typedef uint64_t b64;

// function keywords
#define root_function static
#define inline_function static inline

// Scopes
#define DeferScoped(start, end) for(int _i_ = ((start), 0); _i_ == 0; (_i_ += 1, (end)))

// Arena
struct Arena
{
    u64 pos;
    u64 align;
    u64 resSize;
    u64 cmtSize;
    u64 cmt;
};

struct ArenaTemp
{
    Arena* arena;
    u64 pos;
};

root_function Arena*
ArenaAlloc(u64 size);

root_function void
ArenaDealloc(Arena* arena);

root_function void*
ArenaPush(Arena* arena, u64 size);

root_function void*
ArenaPushZero(Arena* arena, u64 size);

root_function void
ArenaPop(Arena* arena, u64 pos);

root_function void
ArenaReset(Arena* arena);

root_function ArenaTemp
ArenaTempBegin(Arena* arena);

root_function void
ArenaTempEnd(ArenaTemp temp);

// Strings
struct String8
{
    u64 size;
    u8* str;
};

root_function String8
Str8(u8* str, u64 size);

root_function String8
Str8Push(Arena* arena, String8 str);

root_function String8
Str8NullTermFromStr8(String8 str);

root_function u8*
StrFromU128(Arena* arena, u128 num);

root_function String8
Str8(Arena* arena, const char* str, ...);

root_function char*
CharFromStr8(String8 str);

// geometry
enum Axis2
{
    Axis2_X,
    Axis2_Y,
    Axis2_COUNT
};

template <typename T> struct Vec2
{
    union
    {
        struct
        {
            T x;
            T y;
        };
        T data[2];
    };

    Vec2() : x(0), y(0)
    {
    }

    Vec2(const T& x, const T& y) : x(x), y(y)
    {
    }

    Vec2(const T&& x, const T&& y) : x(x), y(y)
    {
    }

    inline Vec2
    operator+(const Vec2& other) const
    {
        return {x + other.x, y + other.y};
    }

    inline Vec2
    operator+(const f32 adder) const
    {
        return {x + adder, y + adder};
    }

    inline Vec2
    operator-(const Vec2& other) const
    {
        return {x - other.x, y - other.y};
    }

    inline Vec2
    operator-(const f32 substractor) const
    {
        return {x - substractor, y - substractor};
    }

    inline Vec2
    operator/(const f32& divisor) const
    {
        return Vec2(x / divisor, y / divisor);
    }

    inline Vec2
    operator/(const Vec2<f32>& divisor) const
    {
        return Vec2(x / divisor.x, y / divisor.y);
    }

    inline Vec2
    operator*(const f32& multiplier) const
    {
        return Vec2(x * multiplier, y * multiplier);
    }

    inline Vec2
    operator*(const Vec2<T>& multiplier) const
    {
        return Vec2(x * multiplier.x, y * multiplier.y);
    }

    inline bool
    operator<(const T& comparator) const
    {
        return (x < comparator && y < comparator);
    }

    inline bool
    operator>(const T& comparator) const
    {
        return (x > comparator && y > comparator);
    }

    inline bool
    operator<=(const Vec2<T>& comparator) const
    {
        return (x <= comparator.x && y <= comparator.y);
    }

    inline bool
    operator>=(const Vec2<T>& comparator) const
    {
        return (x >= comparator.x && y >= comparator.y);
    }

    template <typename To> operator Vec2<To>() const
    {
        return Vec2<To>(static_cast<To>(x), static_cast<To>(y));
    }

    inline T&
    operator[](const u32 i)
    {
        return data[i];
    }
};

union F32Vec4
{
    struct
    {
        f32 x;
        f32 y;
        f32 z;
        f32 w;
    } axis;
    struct
    {
        f32 r;
        f32 g;
        f32 b;
        f32 a;
    } color;

    struct
    {
        Vec2<f32> p0;
        Vec2<f32> p1;
    } point;

    f32 data[4];
};

struct Vec3
{
    f32 x;
    f32 y;
    f32 z;

    Vec3(f32 x, f32 y, f32 z) : x(x), y(y), z(z)
    {
    }

    Vec3
    operator+(const Vec3& other) const
    {
        return {x + other.x, y + other.y, z + other.z};
    }

    Vec3
    operator-(const Vec3& other) const
    {
        return {x - other.x, y - other.y, z - other.z};
    }

    Vec3
    operator/(const f32 divisor)
    {
        return Vec3(x / divisor, x / divisor, z / divisor);
    }
};

#define no_name_mangle extern "C"
// sanitization
#if ASAN_ENABLED
#pragma comment(lib, "clang_rt.asan-x86_64.lib")
no_name_mangle void __asan_poison_memory_region(void const volatile *addr, size_t size);
no_name_mangle void __asan_unpoison_memory_region(void const volatile *addr, size_t size);
# define AsanPoisonMemoryRegion(addr, size)   __asan_poison_memory_region((addr), (size))
# define AsanUnpoisonMemoryRegion(addr, size) __asan_unpoison_memory_region((addr), (size))
#else
# define AsanPoisonMemoryRegion(addr, size)   ((void)(addr), (void)(size))
# define AsanUnpoisonMemoryRegion(addr, size) ((void)(addr), (void)(size))
#endif

// scratch arena
root_function ArenaTemp
ArenaScratchGet();
root_function ArenaTemp
ArenaScratchGet(Arena** conflicts, u64 conflict_count);

#define MemoryCopy(dst, src, size) memcpy((dst), (src), (size))
#define MemorySet(dst, byte, size) memset((dst), (byte), (size))

#define MemoryZero(ptr, size) MemorySet((ptr), 0, (size))
#define MemoryZeroStruct(ptr) MemoryZero((ptr), sizeof(*(ptr)))
#define MemoryZeroArray(arr) MemoryZero((arr), sizeof(arr))

#define PushArray(arena, type, count) ((type*)ArenaPush((arena), sizeof(type) * (count)))
#define PushStruct(arena, type) PushArray((arena), type, 1)
#define PushArrayZero(arena, type, count) ((type*)ArenaPushZero((arena), sizeof(type) * (count)))
#define PushStructZero(arena, type) PushArrayZero((arena), type, 1)

// align
#define PushArrayAlign(arena, type, count)                                                         \
    ((type*)ArenaPushAlign((arena), sizeof(type) * (count), (Max(8, AlignOf(type)))))
#define PushStructAlign(arena, type) PushArrayAlign((arena), type, 1)
#define PushArrayZeroAlign(arena, type, count)                                                     \
    ((type*)ArenaPushZeroAlign((arena), sizeof(type) * (count), (Max(8, AlignOf(type)))))
#define PushStructZeroAlign(arena, type) PushArrayZeroAlign((arena), type, 1)

// Buffers
#define BufferDec(type)                                                                            \
    struct type##_Buffer                                                                           \
    {                                                                                              \
        type* data;                                                                                \
        u64 size;                                                                                  \
    };                                                                                             \
    type##_Buffer type##_Buffer_Alloc(Arena* arena, u64 count)

#define BufferImpl(type)                                                                           \
    type##_Buffer type##_Buffer_Alloc(Arena* arena, u64 count)                                     \
    {                                                                                              \
        type##_Buffer buffer = {0};                                                                \
        buffer.data = PushArray(arena, type, count);                                               \
        buffer.size = count;                                                                       \
        return buffer;                                                                             \
    }

struct Buffer
{
    u8* data;
    u64 size;
};

BufferDec(u16);
BufferDec(String8);

#define KILOBYTE(n) ((n) * 1024ULL)
#define MEGABYTE(n) (KILOBYTE(n) * 1024ULL)
#define GIGABYTE(n) (n * 1024ULL * 1024ULL * 1024ULL)

#define ArrayCount(a) (sizeof((a)) / sizeof((a[0])))

// linked list helpers
#define CheckEmpty(p, o) ((p) == 0 || (p) == (o))
#define IsNull(p) ((p) == 0)
#define SetNull(p) ((p) = 0)

#define QueuePush_NZ(f, l, n, next, zchk, zset)                                                    \
    (zchk(f) ? (((f) = (l) = (n)), zset((n)->next)) : ((l)->next = (n), (l) = (n), zset((n)->next)))
#define QueuePushFront_NZ(f, l, n, next, zchk, zset)                                               \
    (zchk(f) ? (((f) = (l) = (n)), zset((n)->next)) : ((n)->next = (f)), ((f) = (n)))
#define QueuePop_NZ(f, l, next, zset) ((f) == (l) ? (zset(f), zset(l)) : ((f) = (f)->next))
#define StackPush_N(f, n, next) ((n)->next = (f), (f) = (n))
#define StackPop_NZ(f, next, zchk) (zchk(f) ? 0 : ((f) = (f)->next))

#define DLLInsert_NPZ(f, l, p, n, next, prev, zchk, zset)                                          \
    (zchk(f) ? (((f) = (l) = (n)), zset((n)->next), zset((n)->prev))                               \
     : zchk(p)                                                                                     \
         ? (zset((n)->prev), (n)->next = (f), (zchk(f) ? (0) : ((f)->prev = (n))), (f) = (n))      \
         : ((zchk((p)->next) ? (0) : (((p)->next->prev) = (n))), (n)->next = (p)->next,            \
            (n)->prev = (p), (p)->next = (n), ((p) == (l) ? (l) = (n) : (0))))
#define DLLPushBack_NPZ(f, l, n, next, prev, zchk, zset)                                           \
    DLLInsert_NPZ(f, l, l, n, next, prev, zchk, zset)
#define DLLRemove_NPZ(f, l, n, next, prev, zchk, zset)                                             \
    (((f) == (n))   ? ((f) = (f)->next, (zchk(f) ? (zset(l)) : zset((f)->prev)))                   \
     : ((l) == (n)) ? ((l) = (l)->prev, (zchk(l) ? (zset(f)) : zset((l)->next)))                   \
                    : ((zchk((n)->next) ? (0) : ((n)->next->prev = (n)->prev)),                    \
                       (zchk((n)->prev) ? (0) : ((n)->prev->next = (n)->next))))

#define QueuePush(f, l, n) QueuePush_NZ(f, l, n, next, IsNull, SetNull)
#define QueuePushFront(f, l, n) QueuePushFront_NZ(f, l, n, next, IsNull, SetNull)
#define QueuePop(f, l) QueuePop_NZ(f, l, next, SetNull)
#define StackPush(f, n) StackPush_N(f, n, next)
#define StackPop(f) StackPop_NZ(f, next, IsNull)
#define DLLPushBack(f, l, n) DLLPushBack_NPZ(f, l, n, next, prev, IsNull, SetNull)
#define DLLPushFront(f, l, n) DLLPushBack_NPZ(l, f, n, prev, next, IsNull, SetNull)
#define DLLInsert(f, l, p, n) DLLInsert_NPZ(f, l, p, n, next, prev, IsNull, SetNull)
#define DLLRemove(f, l, n) DLLRemove_NPZ(f, l, n, next, prev, IsNull, SetNull)

// string manipulation helpers
#define CStrEqual(a, b) (!strcmp((a), (b)))

// compiler specifics
#ifdef __GNUC__
#define LSBIndex(n) (__builtin_ffs((n)) - 1)
#define AlignOf(n) __alignof__(n)
#elif defined(_MSC_VER)
#define LSBIndex(n) (31 - __lzcnt(n))  
#define AlignOf(n) __alignof(n)  
#else
#error compiler not supported
#endif

// small helpers
#define Min(a, b) ((a) < (b) ? (a) : (b))
#define Max(a, b) ((a) > (b) ? (a) : (b))
template <typename N>
root_function N
Clamp(N d, N min, N max);
