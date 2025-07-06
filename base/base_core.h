// Copyright (c) 2024 Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef BASE_CORE_H
#define BASE_CORE_H

////////////////////////////////
//~ rjf: Foreign Includes

#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

////////////////////////////////
//~ rjf: Codebase Keywords

#define local_persist static

#if COMPILER_MSVC || (COMPILER_CLANG && OS_WINDOWS)
#pragma section(".rdata$", read)
#define read_only __declspec(allocate(".rdata$"))
#elif (COMPILER_CLANG && OS_LINUX)
#define read_only __attribute__((section(".rodata")))
#else
// NOTE(rjf): I don't know of a useful way to do this in GCC land.
// __attribute__((section(".rodata"))) looked promising, but it introduces a
// strange warning about malformed section attributes, and it doesn't look
// like writing to that section reliably produces access violations, strangely
// enough. (It does on Clang)
#define read_only
#endif

#if COMPILER_MSVC
#define thread_static __declspec(thread)
#elif COMPILER_CLANG || COMPILER_GCC
#define thread_static __thread
#endif

#if COMPILER_MSVC
#define force_inline __forceinline
#elif COMPILER_CLANG || COMPILER_GCC
#define force_inline __attribute__((always_inline))
#endif

////////////////////////////////
//~ rjf: Linkage Keyword Macros

#if OS_WINDOWS
#define shared_function C_LINKAGE __declspec(dllexport)
#else
#define shared_function C_LINKAGE
#endif

#if LANG_CPP
#define C_LINKAGE_BEGIN                                                                            \
    extern "C"                                                                                     \
    {
#define C_LINKAGE_END }
#define C_LINKAGE extern "C"
#else
#define C_LINKAGE_BEGIN
#define C_LINKAGE_END
#define C_LINKAGE
#endif

////////////////////////////////
//~ rjf: Units

#define KB(n) (((U64)(n)) << 10)
#define MB(n) (((U64)(n)) << 20)
#define GB(n) (((U64)(n)) << 30)
#define TB(n) (((U64)(n)) << 40)
#define Thousand(n) ((n) * 1000)
#define Million(n) ((n) * 1000000)
#define Billion(n) ((n) * 1000000000)

////////////////////////////////
//~ rjf: Branch Predictor Hints

#if defined(__clang__)
#define Expect(expr, val) __builtin_expect((expr), (val))
#else
#define Expect(expr, val) (expr)
#endif

#define Likely(expr) Expect(expr, 1)
#define Unlikely(expr) Expect(expr, 0)

////////////////////////////////
//~ rjf: Clamps, Mins, Maxes

#define Min(A, B) (((A) < (B)) ? (A) : (B))
#define Max(A, B) (((A) > (B)) ? (A) : (B))
#define ClampTop(A, X) Min(A, X)
#define ClampBot(X, B) Max(X, B)
#define Clamp(A, X, B) (((X) < (A)) ? (A) : ((X) > (B)) ? (B) : (X))

////////////////////////////////
//~ rjf: Type -> Alignment

#if COMPILER_MSVC
#define AlignOf(T) __alignof(T)
#elif COMPILER_CLANG
#define AlignOf(T) __alignof(T)
#elif COMPILER_GCC
#define AlignOf(T) __alignof__(T)
#else
#error AlignOf not defined for this compiler.
#endif

////////////////////////////////
//~ rjf: Member Offsets

#define Member(T, m) (((T*)0)->m)
#define OffsetOf(T, m) IntFromPtr(&Member(T, m))
#define MemberFromOffset(T, ptr, off) (T)((((U8*)ptr) + (off)))
#define CastFromMember(T, m, ptr) (T*)(((U8*)ptr) - OffsetOf(T, m))

////////////////////////////////
//~ rjf: For-Loop Construct Macros

#define DeferLoop(begin, end) for (int _i_ = ((begin), 0); !_i_; _i_ += 1, (end))
#define DeferLoopChecked(begin, end)                                                               \
    for (int _i_ = 2 * !(begin); (_i_ == 2 ? ((end), 0) : !_i_); _i_ += 1, (end))

#define EachIndex(it, count) (U64 it = 0; it < (count); it += 1)
#define EachElement(it, array) (U64 it = 0; it < ArrayCount(array); it += 1)
#define EachEnumVal(type, it) (type it = (type)0; it < type##_COUNT; it = (type)(it + 1))
#define EachNonZeroEnumVal(type, it) (type it = (type)1; it < type##_COUNT; it = (type)(it + 1))

////////////////////////////////
//~ rjf: Memory Operation Macros

#define MemoryCopy(dst, src, size) memmove((dst), (src), (size))
#define MemorySet(dst, byte, size) memset((dst), (byte), (size))
#define MemoryCompare(a, b, size) memcmp((a), (b), (size))
#define MemoryStrlen(ptr) strlen(ptr)

#define MemoryCopyStruct(d, s) MemoryCopy((d), (s), sizeof(*(d)))
#define MemoryCopyArray(d, s) MemoryCopy((d), (s), sizeof(d))
#define MemoryCopyTyped(d, s, c) MemoryCopy((d), (s), sizeof(*(d)) * (c))

#define MemoryZero(s, z) memset((s), 0, (z))
#define MemoryZeroStruct(s) MemoryZero((s), sizeof(*(s)))
#define MemoryZeroArray(a) MemoryZero((a), sizeof(a))
#define MemoryZeroTyped(m, c) MemoryZero((m), sizeof(*(m)) * (c))

#define MemoryMatch(a, b, z) (MemoryCompare((a), (b), (z)) == 0)
#define MemoryMatchStruct(a, b) MemoryMatch((a), (b), sizeof(*(a)))
#define MemoryMatchArray(a, b) MemoryMatch((a), (b), sizeof(a))

#define MemoryRead(T, p, e) (((p) + sizeof(T) <= (e)) ? (*(T*)(p)) : (0))
#define MemoryConsume(T, p, e)                                                                     \
    (((p) + sizeof(T) <= (e)) ? ((p) += sizeof(T), *(T*)((p) - sizeof(T))) : ((p) = (e), 0))

////////////////////////////////
//~ rjf: Asserts

#if COMPILER_MSVC
#define Trap() __debugbreak()
#elif COMPILER_CLANG || COMPILER_GCC
#define Trap() __builtin_trap()
#else
#error Unknown trap intrinsic for this compiler.
#endif

#define AssertAlways(x)                                                                            \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            Trap();                                                                                \
        }                                                                                          \
    } while (0)
#if BUILD_DEBUG
#define Assert(x) AssertAlways(x)
#else
#define Assert(x) (void)(x)
#endif
#define InvalidPath Assert(!"Invalid Path!")
#define NotImplemented Assert(!"Not Implemented!")
#define NoOp ((void)0)
#define StaticAssert(C, ID) static U8 Glue(ID, __LINE__)[(C) ? 1 : -1]

////////////////////////////////
//~ rjf: Atomic Operations

#if COMPILER_MSVC
#include <intrin.h>
#if ARCH_X64
#define ins_atomic_u64_eval(x) *((volatile U64*)(x))
#define ins_atomic_u64_inc_eval(x) InterlockedIncrement64((volatile __int64*)(x))
#define ins_atomic_u64_dec_eval(x) InterlockedDecrement64((volatile __int64*)(x))
#define ins_atomic_u64_eval_assign(x, c) InterlockedExchange64((volatile __int64*)(x), (c))
#define ins_atomic_u64_add_eval(x, c) InterlockedAdd64((volatile __int64*)(x), c)
#define ins_atomic_u64_eval_cond_assign(x, k, c)                                                   \
    InterlockedCompareExchange64((volatile __int64*)(x), (k), (c))
#define ins_atomic_u32_eval(x) *((volatile U32*)(x))
#define ins_atomic_u32_inc_eval(x) InterlockedIncrement((volatile LONG*)x)
#define ins_atomic_u32_eval_assign(x, c) InterlockedExchange((volatile LONG*)(x), (c))
#define ins_atomic_u32_eval_cond_assign(x, k, c)                                                   \
    InterlockedCompareExchange((volatile LONG*)(x), (k), (c))
#define ins_atomic_u32_add_eval(x, c) InterlockedAdd((volatile LONG*)(x), c)
#else
#error Atomic intrinsics not defined for this compiler / architecture combination.
#endif
#elif COMPILER_CLANG || COMPILER_GCC
#define ins_atomic_u64_eval(x) __atomic_load_n(x, __ATOMIC_SEQ_CST)
#define ins_atomic_u64_inc_eval(x) (__atomic_fetch_add((volatile U64*)(x), 1, __ATOMIC_SEQ_CST) + 1)
#define ins_atomic_u64_dec_eval(x) (__atomic_fetch_sub((volatile U64*)(x), 1, __ATOMIC_SEQ_CST) - 1)
#define ins_atomic_u64_eval_assign(x, c) __atomic_exchange_n(x, c, __ATOMIC_SEQ_CST)
#define ins_atomic_u64_add_eval(x, c)                                                              \
    (__atomic_fetch_add((volatile U64*)(x), c, __ATOMIC_SEQ_CST) + (c))
#define ins_atomic_u64_eval_cond_assign(x, k, c)                                                   \
    ({                                                                                             \
        U64 _new = (c);                                                                            \
        __atomic_compare_exchange_n((volatile U64*)(x), &_new, (k), 0, __ATOMIC_SEQ_CST,           \
                                    __ATOMIC_SEQ_CST);                                             \
        _new;                                                                                      \
    })
#define ins_atomic_u32_eval(x) __atomic_load_n(x, __ATOMIC_SEQ_CST)
#define ins_atomic_u32_inc_eval(x) (__atomic_fetch_add((volatile U32*)(x), 1, __ATOMIC_SEQ_CST) + 1)
#define ins_atomic_u32_add_eval(x, c)                                                              \
    (__atomic_fetch_add((volatile U32*)(x), c, __ATOMIC_SEQ_CST) + (c))
#define ins_atomic_u32_eval_assign(x, c) __atomic_exchange_n(x, c, __ATOMIC_SEQ_CST)
#define ins_atomic_u32_eval_cond_assign(x, k, c)                                                   \
    ({                                                                                             \
        U32 _new = (c);                                                                            \
        __atomic_compare_exchange_n((volatile U32*)(x), &_new, (k), 0, __ATOMIC_SEQ_CST,           \
                                    __ATOMIC_SEQ_CST);                                             \
        _new;                                                                                      \
    })
#else
#error Atomic intrinsics not defined for this compiler / architecture.
#endif

#if ARCH_64BIT
#define ins_atomic_ptr_eval_cond_assign(x, k, c)                                                   \
    (void*)ins_atomic_u64_eval_cond_assign((volatile U64*)(x), (U64)(k), (U64)(c))
#define ins_atomic_ptr_eval_assign(x, c)                                                           \
    (void*)ins_atomic_u64_eval_assign((volatile U64*)(x), (U64)(c))
#define ins_atomic_ptr_eval(x) (void*)ins_atomic_u64_eval((volatile U64*)x)
#elif ARCH_32BIT
#define ins_atomic_ptr_eval_cond_assign(x, k, c)                                                   \
    (void*)ins_atomic_u32_eval_cond_assign((volatile U32*)(x), (U32)(k), (U32)(c))
#define ins_atomic_ptr_eval_assign(x, c)                                                           \
    (void*)ins_atomic_u32_eval_assign((volatile U32*)(x), (U32)(c))
#define ins_atomic_ptr_eval(x) (void*)ins_atomic_u32_eval((volatile U32*)x)
#else
#error Atomic intrinsics for pointers not defined for this architecture.
#endif

////////////////////////////////
//~ rjf: Linked List Building Macros

//- rjf: linked list macro helpers
#define CheckNil(nil, p) ((p) == 0 || (p) == nil)
#define SetNil(nil, p) ((p) = nil)

//- rjf: doubly-linked-lists
#define DLLInsert_NPZ(nil, f, l, p, n, next, prev)                                                 \
    (CheckNil(nil, f)   ? ((f) = (l) = (n), SetNil(nil, (n)->next), SetNil(nil, (n)->prev))        \
     : CheckNil(nil, p) ? ((n)->next = (f), (f)->prev = (n), (f) = (n), SetNil(nil, (n)->prev))    \
     : ((p) == (l))                                                                                \
         ? ((l)->next = (n), (n)->prev = (l), (l) = (n), SetNil(nil, (n)->next))                   \
         : (((!CheckNil(nil, p) && CheckNil(nil, (p)->next)) ? (0) : ((p)->next->prev = (n))),     \
            ((n)->next = (p)->next), ((p)->next = (n)), ((n)->prev = (p))))
#define DLLPushBack_NPZ(nil, f, l, n, next, prev) DLLInsert_NPZ(nil, f, l, l, n, next, prev)
#define DLLPushFront_NPZ(nil, f, l, n, next, prev) DLLInsert_NPZ(nil, l, f, f, n, prev, next)
#define DLLRemove_NPZ(nil, f, l, n, next, prev)                                                    \
    (((n) == (f) ? (f) = (n)->next : (0)), ((n) == (l) ? (l) = (l)->prev : (0)),                   \
     (CheckNil(nil, (n)->prev) ? (0) : ((n)->prev->next = (n)->next)),                             \
     (CheckNil(nil, (n)->next) ? (0) : ((n)->next->prev = (n)->prev)))

//- rjf: singly-linked, doubly-headed lists (queues)
#define SLLQueuePush_NZ(nil, f, l, n, next)                                                        \
    (CheckNil(nil, f) ? ((f) = (l) = (n), SetNil(nil, (n)->next))                                  \
                      : ((l)->next = (n), (l) = (n), SetNil(nil, (n)->next)))
#define SLLQueuePushFront_NZ(nil, f, l, n, next)                                                   \
    (CheckNil(nil, f) ? ((f) = (l) = (n), SetNil(nil, (n)->next)) : ((n)->next = (f), (f) = (n)))
#define SLLQueuePop_NZ(nil, f, l, next)                                                            \
    ((f) == (l) ? (SetNil(nil, f), SetNil(nil, l)) : ((f) = (f)->next))

//- rjf: singly-linked, singly-headed lists (stacks)
#define SLLStackPush_N(f, n, next) ((n)->next = (f), (f) = (n))
#define SLLStackPop_N(f, next) ((f) = (f)->next)

//- rjf: doubly-linked-list helpers
#define DLLInsert_NP(f, l, p, n, next, prev) DLLInsert_NPZ(0, f, l, p, n, next, prev)
#define DLLPushBack_NP(f, l, n, next, prev) DLLPushBack_NPZ(0, f, l, n, next, prev)
#define DLLPushFront_NP(f, l, n, next, prev) DLLPushFront_NPZ(0, f, l, n, next, prev)
#define DLLRemove_NP(f, l, n, next, prev) DLLRemove_NPZ(0, f, l, n, next, prev)
#define DLLInsert(f, l, p, n) DLLInsert_NPZ(0, f, l, p, n, next, prev)
#define DLLPushBack(f, l, n) DLLPushBack_NPZ(0, f, l, n, next, prev)
#define DLLPushFront(f, l, n) DLLPushFront_NPZ(0, f, l, n, next, prev)
#define DLLRemove(f, l, n) DLLRemove_NPZ(0, f, l, n, next, prev)

//- rjf: singly-linked, doubly-headed list helpers
#define SLLQueuePush_N(f, l, n, next) SLLQueuePush_NZ(0, f, l, n, next)
#define SLLQueuePushFront_N(f, l, n, next) SLLQueuePushFront_NZ(0, f, l, n, next)
#define SLLQueuePop_N(f, l, next) SLLQueuePop_NZ(0, f, l, next)
#define SLLQueuePush(f, l, n) SLLQueuePush_NZ(0, f, l, n, next)
#define SLLQueuePushFront(f, l, n) SLLQueuePushFront_NZ(0, f, l, n, next)
#define SLLQueuePop(f, l) SLLQueuePop_NZ(0, f, l, next)

//- rjf: singly-linked, singly-headed list helpers
#define SLLStackPush(f, n) SLLStackPush_N(f, n, next)
#define SLLStackPop(f) SLLStackPop_N(f, next)

////////////////////////////////
//~ rjf: Address Sanitizer Markup

#if COMPILER_MSVC
#if defined(__SANITIZE_ADDRESS__)
#define ASAN_ENABLED 1
#define NO_ASAN __declspec(no_sanitize_address)
#else
#define NO_ASAN
#endif
#elif COMPILER_CLANG
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
#define ASAN_ENABLED 1
#endif
#endif
#define NO_ASAN __attribute__((no_sanitize("address")))
#else
#define NO_ASAN
#endif

#if ASAN_ENABLED
#pragma comment(lib, "clang_rt.asan_dynamic-x86_64.lib")
C_LINKAGE void
__asan_poison_memory_region(void const volatile* addr, size_t size);
C_LINKAGE void
__asan_unpoison_memory_region(void const volatile* addr, size_t size);
#define AsanPoisonMemoryRegion(addr, size) __asan_poison_memory_region((addr), (size))
#define AsanUnpoisonMemoryRegion(addr, size) __asan_unpoison_memory_region((addr), (size))
#else
#define AsanPoisonMemoryRegion(addr, size) ((void)(addr), (void)(size))
#define AsanUnpoisonMemoryRegion(addr, size) ((void)(addr), (void)(size))
#endif

////////////////////////////////
//~ rjf: Misc. Helper Macros

#define Stringify_(S) #S
#define Stringify(S) Stringify_(S)

#define Glue_(A, B) A##B
#define Glue(A, B) Glue_(A, B)

#define ArrayCount(a) (sizeof(a) / sizeof((a)[0]))

#define CeilIntegerDiv(a, b) (((a) + (b) - 1) / (b))

#define Swap(T, a, b)                                                                              \
    do                                                                                             \
    {                                                                                              \
        T t__ = a;                                                                                 \
        a = b;                                                                                     \
        b = t__;                                                                                   \
    } while (0)

#if ARCH_64BIT
#define IntFromPtr(ptr) ((U64)(ptr))
#elif ARCH_32BIT
#define IntFromPtr(ptr) ((U32)(ptr))
#else
#error Missing pointer-to-integer cast for this architecture.
#endif
#define PtrFromInt(i) (void*)((U8*)0 + (i))

#define Compose64Bit(a, b) ((((U64)a) << 32) | ((U64)b));
#define AlignPow2(x, b) (((x) + (b) - 1) & (~((b) - 1)))
#define AlignDownPow2(x, b) ((x) & (~((b) - 1)))
#define AlignPadPow2(x, b) ((0 - (x)) & ((b) - 1))
#define IsPow2(x) ((x) != 0 && ((x) & ((x) - 1)) == 0)
#define IsPow2OrZero(x) ((((x) - 1) & (x)) == 0)

#define ExtractBit(word, idx) (((word) >> (idx)) & 1)
#define Extract8(word, pos) (((word) >> ((pos) * 8)) & max_U8)
#define Extract16(word, pos) (((word) >> ((pos) * 16)) & max_U16)
#define Extract32(word, pos) (((word) >> ((pos) * 32)) & max_U32)

#if LANG_CPP
#define zero_struct                                                                                \
    {                                                                                              \
    }
#else
#define zero_struct {0}
#endif

#if COMPILER_MSVC && COMPILER_MSVC_YEAR < 2015
#define this_function_name "unknown"
#else
#define this_function_name __func__
#endif

////////////////////////////////
//~ rjf: Base Types

typedef uint8_t U8;
typedef uint16_t U16;
typedef uint32_t U32;
typedef uint64_t U64;
typedef int8_t S8;
typedef int16_t S16;
typedef int32_t S32;
typedef int64_t S64;
typedef S8 B8;
typedef S16 B16;
typedef S32 B32;
typedef S64 B64;
typedef float F32;
typedef double F64;
typedef void
VoidProc(void);
typedef union U128 U128;
union U128
{
    U8 u8[16];
    U16 U16[8];
    U32 U32[4];
    U64 u64[2];
};
typedef union U256 U256;
union U256
{
    U8 u8[32];
    U16 U16[16];
    U32 U32[8];
    U64 u64[4];
    U128 u128[2];
};
typedef union U512 U512;
union U512
{
    U8 u8[64];
    U16 U16[32];
    U32 U32[16];
    U64 u64[8];
    U128 u128[4];
    U256 u256[2];
};

////////////////////////////////
//~ rjf: Basic Types & Spaces

typedef enum Dimension
{
    Dimension_X,
    Dimension_Y,
    Dimension_Z,
    Dimension_W,
} Dimension;

typedef enum Side
{
    Side_Invalid = -1,
    Side_Min,
    Side_Max,
    Side_COUNT,
} Side;
#define side_flip(s) ((Side)(!(s)))

typedef enum Axis2
{
    Axis2_Invalid = -1,
    Axis2_X,
    Axis2_Y,
    Axis2_COUNT,
} Axis2;
#define axis2_flip(a) ((Axis2)(!(a)))

typedef enum Corner
{
    Corner_Invalid = -1,
    Corner_00,
    Corner_01,
    Corner_10,
    Corner_11,
    Corner_COUNT
} Corner;

typedef enum Dir2
{
    Dir2_Invalid = -1,
    Dir2_Left,
    Dir2_Up,
    Dir2_Right,
    Dir2_Down,
    Dir2_COUNT
} Dir2;
#define axis2_from_dir2(d) (((d) & 1) ? Axis2_Y : Axis2_X)
#define side_from_dir2(d) (((d) < Dir2_Right) ? Side_Min : Side_Max)

////////////////////////////////
//~ rjf: Toolchain/Environment Enums

typedef enum OperatingSystem
{
    OperatingSystem_Null,
    OperatingSystem_Windows,
    OperatingSystem_Linux,
    OperatingSystem_Mac,
    OperatingSystem_COUNT,
} OperatingSystem;

typedef enum ImageType
{
    Image_Null,
    Image_CoffPe,
    Image_Elf32,
    Image_Elf64,
    Image_Macho
} ImageType;

typedef enum Arch
{
    Arch_Null,
    Arch_x64,
    Arch_x86,
    Arch_arm64,
    Arch_arm32,
    Arch_COUNT,
} Arch;

typedef enum Compiler
{
    Compiler_Null,
    Compiler_msvc,
    Compiler_gcc,
    Compiler_clang,
    Compiler_COUNT,
} Compiler;

////////////////////////////////
//~ rjf: Text 2D Coordinates & Ranges

typedef struct TxtPt TxtPt;
struct TxtPt
{
    S64 line;
    S64 column;
};

typedef struct TxtRng TxtRng;
struct TxtRng
{
    TxtPt min;
    TxtPt max;
};

////////////////////////////////
//~ Globally Unique Ids

typedef union Guid Guid;
union Guid
{
    struct
    {
        U32 data1;
        U16 data2;
        U16 data3;
        U8 data4[8];
    };
    U8 v[16];
};
StaticAssert(sizeof(Guid) == 16, g_guid_size_check);

////////////////////////////////
//~ Arrays

typedef struct U16Array U16Array;
struct U16Array
{
    U64 count;
    U16* v;
};
typedef struct U32Array U32Array;
struct U32Array
{
    U64 count;
    U32* v;
};
typedef struct U64Array U64Array;
struct U64Array
{
    U64 count;
    U64* v;
};
typedef struct U128Array U128Array;
struct U128Array
{
    U64 count;
    U128* v;
};

////////////////////////////////
//~ NOTE(allen): Constants

static U32 sign32 = 0x80000000;
static U32 exponent32 = 0x7F800000;
static U32 mantissa32 = 0x007FFFFF;

static F32 big_golden32 = 1.61803398875f;
static F32 small_golden32 = 0.61803398875f;

static F32 pi32 = 3.1415926535897f;

static F64 machine_epsilon64 = 4.94065645841247e-324;

static U64 max_U64 = 0xffffffffffffffffull;
static U32 max_U32 = 0xffffffff;
static U16 max_U16 = 0xffff;
static U8 max_U8 = 0xff;

static S64 max_S64 = (S64)0x7fffffffffffffffull;
static S32 max_S32 = (S32)0x7fffffff;
static S16 max_S16 = (S16)0x7fff;
static S8 max_S8 = (S8)0x7f;

static S64 min_S64 = (S64)0xffffffffffffffffull;
static S32 min_S32 = (S32)0xffffffff;
static S16 min_S16 = (S16)0xffff;
static S8 min_S8 = (S8)0xff;

static const U32 bitmask1 = 0x00000001;
static const U32 bitmask2 = 0x00000003;
static const U32 bitmask3 = 0x00000007;
static const U32 bitmask4 = 0x0000000f;
static const U32 bitmask5 = 0x0000001f;
static const U32 bitmask6 = 0x0000003f;
static const U32 bitmask7 = 0x0000007f;
static const U32 bitmask8 = 0x000000ff;
static const U32 bitmask9 = 0x000001ff;
static const U32 bitmask10 = 0x000003ff;
static const U32 bitmask11 = 0x000007ff;
static const U32 bitmask12 = 0x00000fff;
static const U32 bitmask13 = 0x00001fff;
static const U32 bitmask14 = 0x00003fff;
static const U32 bitmask15 = 0x00007fff;
static const U32 bitmask16 = 0x0000ffff;
static const U32 bitmask17 = 0x0001ffff;
static const U32 bitmask18 = 0x0003ffff;
static const U32 bitmask19 = 0x0007ffff;
static const U32 bitmask20 = 0x000fffff;
static const U32 bitmask21 = 0x001fffff;
static const U32 bitmask22 = 0x003fffff;
static const U32 bitmask23 = 0x007fffff;
static const U32 bitmask24 = 0x00ffffff;
static const U32 bitmask25 = 0x01ffffff;
static const U32 bitmask26 = 0x03ffffff;
static const U32 bitmask27 = 0x07ffffff;
static const U32 bitmask28 = 0x0fffffff;
static const U32 bitmask29 = 0x1fffffff;
static const U32 bitmask30 = 0x3fffffff;
static const U32 bitmask31 = 0x7fffffff;
static const U32 bitmask32 = 0xffffffff;

static const U64 bitmask33 = 0x00000001ffffffffull;
static const U64 bitmask34 = 0x00000003ffffffffull;
static const U64 bitmask35 = 0x00000007ffffffffull;
static const U64 bitmask36 = 0x0000000fffffffffull;
static const U64 bitmask37 = 0x0000001fffffffffull;
static const U64 bitmask38 = 0x0000003fffffffffull;
static const U64 bitmask39 = 0x0000007fffffffffull;
static const U64 bitmask40 = 0x000000ffffffffffull;
static const U64 bitmask41 = 0x000001ffffffffffull;
static const U64 bitmask42 = 0x000003ffffffffffull;
static const U64 bitmask43 = 0x000007ffffffffffull;
static const U64 bitmask44 = 0x00000fffffffffffull;
static const U64 bitmask45 = 0x00001fffffffffffull;
static const U64 bitmask46 = 0x00003fffffffffffull;
static const U64 bitmask47 = 0x00007fffffffffffull;
static const U64 bitmask48 = 0x0000ffffffffffffull;
static const U64 bitmask49 = 0x0001ffffffffffffull;
static const U64 bitmask50 = 0x0003ffffffffffffull;
static const U64 bitmask51 = 0x0007ffffffffffffull;
static const U64 bitmask52 = 0x000fffffffffffffull;
static const U64 bitmask53 = 0x001fffffffffffffull;
static const U64 bitmask54 = 0x003fffffffffffffull;
static const U64 bitmask55 = 0x007fffffffffffffull;
static const U64 bitmask56 = 0x00ffffffffffffffull;
static const U64 bitmask57 = 0x01ffffffffffffffull;
static const U64 bitmask58 = 0x03ffffffffffffffull;
static const U64 bitmask59 = 0x07ffffffffffffffull;
static const U64 bitmask60 = 0x0fffffffffffffffull;
static const U64 bitmask61 = 0x1fffffffffffffffull;
static const U64 bitmask62 = 0x3fffffffffffffffull;
static const U64 bitmask63 = 0x7fffffffffffffffull;
static const U64 bitmask64 = 0xffffffffffffffffull;

static const U32 bit1 = (1 << 0);
static const U32 bit2 = (1 << 1);
static const U32 bit3 = (1 << 2);
static const U32 bit4 = (1 << 3);
static const U32 bit5 = (1 << 4);
static const U32 bit6 = (1 << 5);
static const U32 bit7 = (1 << 6);
static const U32 bit8 = (1 << 7);
static const U32 bit9 = (1 << 8);
static const U32 bit10 = (1 << 9);
static const U32 bit11 = (1 << 10);
static const U32 bit12 = (1 << 11);
static const U32 bit13 = (1 << 12);
static const U32 bit14 = (1 << 13);
static const U32 bit15 = (1 << 14);
static const U32 bit16 = (1 << 15);
static const U32 bit17 = (1 << 16);
static const U32 bit18 = (1 << 17);
static const U32 bit19 = (1 << 18);
static const U32 bit20 = (1 << 19);
static const U32 bit21 = (1 << 20);
static const U32 bit22 = (1 << 21);
static const U32 bit23 = (1 << 22);
static const U32 bit24 = (1 << 23);
static const U32 bit25 = (1 << 24);
static const U32 bit26 = (1 << 25);
static const U32 bit27 = (1 << 26);
static const U32 bit28 = (1 << 27);
static const U32 bit29 = (1 << 28);
static const U32 bit30 = (1 << 29);
static const U32 bit31 = (1 << 30);
static const U32 bit32 = (1 << 31);

static const U64 bit33 = (1ull << 32);
static const U64 bit34 = (1ull << 33);
static const U64 bit35 = (1ull << 34);
static const U64 bit36 = (1ull << 35);
static const U64 bit37 = (1ull << 36);
static const U64 bit38 = (1ull << 37);
static const U64 bit39 = (1ull << 38);
static const U64 bit40 = (1ull << 39);
static const U64 bit41 = (1ull << 40);
static const U64 bit42 = (1ull << 41);
static const U64 bit43 = (1ull << 42);
static const U64 bit44 = (1ull << 43);
static const U64 bit45 = (1ull << 44);
static const U64 bit46 = (1ull << 45);
static const U64 bit47 = (1ull << 46);
static const U64 bit48 = (1ull << 47);
static const U64 bit49 = (1ull << 48);
static const U64 bit50 = (1ull << 49);
static const U64 bit51 = (1ull << 50);
static const U64 bit52 = (1ull << 51);
static const U64 bit53 = (1ull << 52);
static const U64 bit54 = (1ull << 53);
static const U64 bit55 = (1ull << 54);
static const U64 bit56 = (1ull << 55);
static const U64 bit57 = (1ull << 56);
static const U64 bit58 = (1ull << 57);
static const U64 bit59 = (1ull << 58);
static const U64 bit60 = (1ull << 59);
static const U64 bit61 = (1ull << 60);
static const U64 bit62 = (1ull << 61);
static const U64 bit63 = (1ull << 62);
static const U64 bit64 = (1ull << 63);

////////////////////////////////
//~ allen: Time

typedef enum WeekDay
{
    WeekDay_Sun,
    WeekDay_Mon,
    WeekDay_Tue,
    WeekDay_Wed,
    WeekDay_Thu,
    WeekDay_Fri,
    WeekDay_Sat,
    WeekDay_COUNT,
} WeekDay;

typedef enum Month
{
    Month_Jan,
    Month_Feb,
    Month_Mar,
    Month_Apr,
    Month_May,
    Month_Jun,
    Month_Jul,
    Month_Aug,
    Month_Sep,
    Month_Oct,
    Month_Nov,
    Month_Dec,
    Month_COUNT,
} Month;

typedef struct DateTime DateTime;
struct DateTime
{
    U16 micro_sec; // [0,999]
    U16 msec;      // [0,999]
    U16 sec;       // [0,60]
    U16 min;       // [0,59]
    U16 hour;      // [0,24]
    U16 day;       // [0,30]
    union
    {
        WeekDay week_day;
        U32 wday;
    };
    union
    {
        Month month;
        U32 mon;
    };
    U32 year; // 1 = 1 CE, 0 = 1 BC
};

typedef U64 DenseTime;

////////////////////////////////
//~ allen: Files

typedef U32 FilePropertyFlags;
enum
{
    FilePropertyFlag_IsFolder = (1 << 0),
};

typedef struct FileProperties FileProperties;
struct FileProperties
{
    U64 size;
    DenseTime modified;
    DenseTime created;
    FilePropertyFlags flags;
};

////////////////////////////////
//~ rjf: Safe Casts

static U16
safe_cast_u16(U32 x);
static U32
safe_cast_u32(U64 x);
static S32
safe_cast_s32(S64 x);

////////////////////////////////
//~ rjf: Large Base Type Functions

static U128
u128_zero(void);
static U128
u128_make(U64 v0, U64 v1);
static B32
u128_match(U128 a, U128 b);

////////////////////////////////
//~ rjf: Bit Patterns

static U32
u32_from_u64_saturate(U64 x);
static U64
u64_up_to_pow2(U64 x);
static S32
extend_sign32(U32 x, U32 size);
static S64
extend_sign64(U64 x, U64 size);

static F32
inf32(void);
static F32
neg_inf32(void);

static U16
bswap_u16(U16 x);
static U32
bswap_u32(U32 x);
static U64
bswap_u64(U64 x);

#if ARCH_LITTLE_ENDIAN
#define from_be_u16(x) bswap_u16(x)
#define from_be_u32(x) bswap_u32(x)
#define from_be_u64(x) bswap_u64(x)
#else
#define from_be_u16(x) (x)
#define from_be_u32(x) (x)
#define from_be_u64(x) (x)
#endif

static U64
count_bits_set32(U32 val);
static U64
count_bits_set64(U64 val);

static U64
ctz32(U32 val);
static U64
ctz64(U64 val);
static U64
clz32(U32 val);
static U64
clz64(U64 val);

////////////////////////////////
//~ rjf: Enum -> Sign

static S32
sign_from_side_S32(Side side);
static F32
sign_from_side_F32(Side side);

////////////////////////////////
//~ rjf: Memory Functions

static B32
memory_is_zero(void* ptr, U64 size);

////////////////////////////////
//~ rjf: Text 2D Coordinate/Range Functions

static TxtPt
txt_pt(S64 line, S64 column);
static B32
txt_pt_match(TxtPt a, TxtPt b);
static B32
txt_pt_less_than(TxtPt a, TxtPt b);
static TxtPt
txt_pt_min(TxtPt a, TxtPt b);
static TxtPt
txt_pt_max(TxtPt a, TxtPt b);
static TxtRng
txt_rng(TxtPt min, TxtPt max);
static TxtRng
txt_rng_intersect(TxtRng a, TxtRng b);
static TxtRng
txt_rng_union(TxtRng a, TxtRng b);
static B32
txt_rng_contains(TxtRng r, TxtPt pt);

////////////////////////////////
//~ rjf: Toolchain/Environment Enum Functions

static U64
bit_size_from_arch(Arch arch);
static U64
max_instruction_size_from_arch(Arch arch);

static OperatingSystem
operating_system_from_context(void);
static Arch
arch_from_context(void);
static Compiler
compiler_from_context(void);

////////////////////////////////
//~ rjf: Time Functions

static DenseTime
dense_time_from_date_time(DateTime date_time);
static DateTime
date_time_from_dense_time(DenseTime time);
static DateTime
date_time_from_micro_seconds(U64 time);
static DateTime
date_time_from_unix_time(U64 unix_time);

////////////////////////////////
//~ rjf: Non-Fancy Ring Buffer Reads/Writes

static U64
ring_write(U8* ring_base, U64 ring_size, U64 ring_pos, void* src_data, U64 src_data_size);
static U64
ring_read(U8* ring_base, U64 ring_size, U64 ring_pos, void* dst_data, U64 read_size);
#define ring_write_struct(ring_base, ring_size, ring_pos, ptr)                                     \
    ring_write((ring_base), (ring_size), (ring_pos), (ptr), sizeof(*(ptr)))
#define ring_read_struct(ring_base, ring_size, ring_pos, ptr)                                      \
    ring_read((ring_base), (ring_size), (ring_pos), (ptr), sizeof(*(ptr)))

////////////////////////////////
//~ rjf: Sorts

#define quick_sort(ptr, count, element_size, cmp_function)                                         \
    qsort((ptr), (count), (element_size), (int (*)(const void*, const void*))(cmp_function))

////////////////////////////////

static U64
u64_array_bsearch(U64* arr, U64 count, U64 value);

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

#endif // BASE_CORE_H
