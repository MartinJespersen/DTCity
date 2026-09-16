#if !defined(SY__BASE_H)
#define SY__BASE_H

////////////////////////////////
// Context Cracking

// untangle compiler, os, & architecture
#if defined(__clang__)
# define SY__COMPILER_CLANG 1

# if defined(_WIN32)
#  define SY__OS_WINDOWS 1
# elif defined(__gnu_linux__)
#  define SY__OS_LINUX 1
# elif defined(__APPLE__) && defined(__MACH__)
#  define SY__OS_MAC 1
# else
#  error missing OS detection
# endif

# if defined(__amd64__)
#  define SY__ARCH_X64 1
// TODO(allen): verify this works on clang
# elif defined(__i386__)
#  define SY__ARCH_X86 1
// TODO(allen): verify this works on clang
# elif defined(__arm__)
#  define SY__ARCH_ARM 1
// TODO(allen): verify this works on clang
# elif defined(__aarch64__)
#  define SY__ARCH_ARM64 1
# else
#  error missing SY__ARCH detection
# endif

#elif defined(_MSC_VER)
# define SY__COMPILER_CL 1

# if defined(_WIN32)
#  define SY__OS_WINDOWS 1
# else
#  error missing OS detection
# endif

# if defined(_M_AMD64)
#  define SY__ARCH_X64 1
# elif defined(_M_I86)
#  define SY__ARCH_X86 1
# elif defined(_M_ARM)
#  define SY__ARCH_ARM 1
// TODO(allen): ARM64?
# else
#  error missing SY__ARCH detection
# endif

#elif defined(__GNUC__)
# define SY__COMPILER_GCC 1

# if defined(_WIN32)
#  define SY__OS_WINDOWS 1
# elif defined(__gnu_linux__)
#  define SY__OS_LINUX 1
# elif defined(__APPLE__) && defined(__MACH__)
#  define SY__OS_MAC 1
# else
#  error missing OS detection
# endif

# if defined(__amd64__)
#  define SY__ARCH_X64 1
# elif defined(__i386__)
#  define SY__ARCH_X86 1
# elif defined(__arm__)
#  define SY__ARCH_ARM 1
# elif defined(__aarch64__)
#  define SY__ARCH_ARM64 1
# else
#  error missing SY__ARCH detection
# endif

#else
# error no context cracking for this compiler
#endif

#if !defined(SY__COMPILER_CL)
# define SY__COMPILER_CL 0
#endif
#if !defined(SY__COMPILER_CLANG)
# define SY__COMPILER_CLANG 0
#endif
#if !defined(SY__COMPILER_GCC)
# define SY__COMPILER_GCC 0
#endif
#if !defined(SY__OS_WINDOWS)
# define SY__OS_WINDOWS 0
#endif
#if !defined(SY__OS_LINUX)
# define SY__OS_LINUX 0
#endif
#if !defined(SY__OS_MAC)
# define SY__OS_MAC 0
#endif
#if !defined(SY__ARCH_X64)
# define SY__ARCH_X64 0
#endif
#if !defined(SY__ARCH_X86)
# define SY__ARCH_X86 0
#endif
#if !defined(SY__ARCH_ARM)
# define SY__ARCH_ARM 0
#endif
#if !defined(SY__ARCH_ARM64)
# define SY__ARCH_ARM64 0
#endif


// language
#if defined(__cplusplus)
# define SY__LANGCXX 1
#else
# define SY__LANGC 1
#endif

#if !defined(SY__LANGCXX)
# define SY__LANGCXX 0
#endif
#if !defined(SY__LANGC)
# define SY__LANGC 0
#endif

// setup pointer size macro
#if SY__ARCH_X64 || SY__ARCH_ARM64
# define SY__ARCH_ADDRSIZE 64
#else
# define SY__ARCH_ADDRSIZE 32
#endif

////////////////////////////////
// GLUE(a,b) STRIFY(s)

#define SY__GLUE__(a,b) a##b
#define SY__GLUE_(a,b)  SY__GLUE__(a,b)
#define SY__GLUE(a,b)   SY__GLUE_(a,b)

#define SY__STRIFY__(s) #s
#define SY__STRIFY_(s)  SY__STRIFY__(s)
#define SY__STRIFY(s)   SY__STRIFY_(s)

////////////////////////////////
// SECTION(<section-name>) <Decl>

#if SY__COMPILER_CLANG || SY__COMPILER_GCC
# if SY__OS_MAC
#  define SY__SEC_R(N) __attribute__((__section__("__TEXT,"N)))
#  define SY__SEC_RW(N) __attribute__((__section__("__READ,"N)))
# else
#  define SY__SEC_R(N) __attribute__((__section__(N)))
#  define SY__SEC_RW(N) __attribute__((__section__(N)))
# endif
#elif SY__COMPILER_CL
# define SY__SEC_R(N) __declspec(allocate(N))
# define SY__SEC_RW(N) __declspec(allocate(N))
#else
# error SY__SEC_R/SY__SEC_RW not defined for this compiler/OS
#endif

// for CL users: #pragma section(<section>,read,write)

////////////////////////////////
// DO_NOT_ELIMINATE

#if SY__OS_WINDOWS

# define SY__DO_NOT_ELIMINATE__S0(N) __pragma(comment(linker,"/include:" #N))
# define SY__DO_NOT_ELIMINATE__S1(N) SY__DO_NOT_ELIMINATE__S0(N)
# define SY__DO_NOT_ELIMINATE(N)     SY__DO_NOT_ELIMINATE__S1(N)

#else

# define SY__DO_NOT_ELIMINATE(N)

#endif

////////////////////////////////
// ALIGN_AS_LIT

#if SY__COMPILER_CL
# define SY__ALIGN_AS_LIT(n) __declspec(align(n))
#elif SY__COMPILER_CLANG || SY__COMPILER_GCC
# define SY__ALIGN_AS_LIT(n) __attribute__(( __aligned__(n) ))
#else
# error SY__ALIGN_AS_LIT not defined for this compiler
#endif

////////////////////////////////
// ROUND_UP_POW2

#define SY__ROUND_UP_POW2(x,p) (((x) + (p) - 1)&~((p) - 1))

////////////////////////////////
// Types

#if !defined(SY__TYPES)
# include <stdint.h>
typedef int8_t   SY__S8;
typedef int16_t  SY__S16;
typedef int32_t  SY__S32;
typedef int64_t  SY__S64;
typedef uint8_t  SY__U8;
typedef uint16_t SY__U16;
typedef uint32_t SY__U32;
typedef uint64_t SY__U64;
typedef SY__S8  SY__B8;
typedef SY__S16 SY__B16;
typedef SY__S32 SY__B32;
typedef SY__S64 SY__B64;
typedef float  SY__F32;
typedef double SY__F64;
# if SY__ARCH_ADDRSIZE == 32
typedef SY__U32 SY__UAddr;
typedef SY__S32 SY__SAddr;
# elif SY__ARCH_ADDRSIZE == 64
typedef SY__U64 SY__UAddr;
typedef SY__S64 SY__SAddr;
# else
#  error SY__UAddr and SY__SAddr not defined for this architecture
# endif
#endif

#endif
