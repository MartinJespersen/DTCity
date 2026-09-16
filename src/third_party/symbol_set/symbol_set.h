#if !defined(SY__H)
#define SY__H

#include "symbol_set.base.h"

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~~~~~~~~~ Copy-Pastable Setup ~~~~~~~~~~~~~~~~~~~~~~~~~
~~ To define a Symbol Set:
~~  copy the code below
~~  replace ZZZ:    to name the symbol set
~~  replace Typezz: to set the type of the symbol set
~~  replace secz:   to name of the data section of the symbol set
~~  pick and complete the sections with "..."s
*/

#if 0

#define SYMBOL_SET_DEFINE ZZZ
#define ZZZ_Type    Typezz
#define ZZZ_elf_section    ".sy.secz"
#define ZZZ_coff_a_section ".sy$secz_a"
#define ZZZ_coff_m_section ".sy$secz_m"
#define ZZZ_coff_z_section ".sy$secz_z"
#define ZZZ_marker  secz
#include "symbol_set.define.h"

// basic struct def with a name
#define ZZZ_DEF(N,...) SyDefine(ZZZ, N) = { ... }

// def with attached function body
//  replace funczz: to prefix namespace unique functions
//  fill ... sections appropriately
#define ZZZ_DEF(N,...) \
static void funczz##N(void*); \
SyDefine(ZZZ, N) = { funczz##N, ... }; \
static void funczz##N(void *ptr)

// nameless def
#define ZZZ_DEF(...)   SyDefineUnnamed(ZZZ) = { ... }

#endif

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~~~ Loop Boilerplates ~~~~~~~~~~~~~~~~~*/

#if 0
{
  for (SyEach(ZZZ, zzz_ptr)){
    zzz_ptr->
  }
}
{
  for (SyEachID(ZZZ, zzz_id)){
    [zzz_id]
  }
}
#endif

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~~~~~~~~~ User Primitives ~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

#define SyType(S) SY__GLUE(S,_Type)

#if SY__OS_WINDOWS
# define SY__SDECL(S) SY__SEC_RW(S##_coff_m_section)
#elif SY__OS_LINUX
# define SY__SDECL(S) SY__SEC_RW(S##_elf_section)
#else
# error missing implementation symbol set main section decl
#endif

#define SyDeclare(S,N)     SY__ALIGN_AS_LIT(8) SY__SDECL(S) SyType(S) SY__SYMBOL(S,N)
#define SyDefine(S,N)      SY__DO_NOT_ELIMINATE(SY__SYMBOL(S,N)) SyDeclare(S,N)
#define SyDefineUnnamed(S) SyDefine(S,SY__GLUE(unnamed,__COUNTER__))

#define SyAddress(S,N) (&SY__SYMBOL(S,N))
#define SyID(S,N)      SyIDFromAddress_Unchecked(S,SyAddress(S,N))
#define SyRaw(S,N)     (SY__UAddr)(SyAddress(S,N))

#if SY__OS_WINDOWS
# define SyFirst(S) ((&SY__FIRST(S)) + 1)
# define SyOpl(S)   (&SY__OPL(S))
#elif SY__OS_LINUX
# define SyFirst(S) (&SY__FIRST(S))
# define SyOpl(S)   (&SY__OPL(S))
#else
# error missing implementation set locator for this OS
#endif

#define SyStride(S) SY__ROUND_UP_POW2(sizeof(SyType(S)), 8)

#define SyCount(S)     (((SY__U8*)SyOpl(S) - (SY__U8*)SyFirst(S))/SyStride(S))
#define SyNext(S,addr) (SyType(S)*)((SY__U8*)addr + SyStride(S))

#define SyEach(S,var)   SyType(S)*var=SyFirst(S); var<SyOpl(S); var=SyNext(S,var)
#define SyEachID(S,var) SY__U32 var=1; var<=SyCount(S); var+=1

#define SyIDFromAddress(S,addr) (SyAddressCheck(S,addr)?SyIDFromAddress_Unchecked(S,addr):0)
#define SyAddressFromID(S,id)   (SyIDCheck(S,id)?SyAddressFromID_Unchecked(S,id):0)

#define SyAddressCheck(S,addr) (SyFirst(S) <= (addr) && (addr) <= SyOpl(S))
#define SyIDCheck(S,id)        (1 <= (id) && (id) <= SyCount(S))

#define SyIDFromAddress_Unchecked(S,addr) ((SY__U32)(1 + ((SY__U8*)(addr) - (SY__U8*)SyFirst(S))/SyStride(S)))
#define SyAddressFromID_Unchecked(S,id)   ((SyType(S)*)((SY__U8*)SyFirst(S) + ((id) - 1)*SyStride(S)))

#define SyIDFromRaw(S,raw)           SyIDFromAddress(S,(SyType(S)*)(raw))
#define SyIDFromRaw_Unchecked(S,raw) SyIDFromAddress_Unchecked(S,(SyType(S)*)(raw))

#define SyFlag(S,N)    (1llu << (SyID(S,N) - 1))

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~~~~ Internal ~~~~~~~~~~~~~~~~~~~~~~*/

#define SY__SYMBOL(S,N) SY__GLUE(S,SY__GLUE(__,N))

#define SY__FIRST_(S) SY__GLUE(syfirst__, S##_marker)
#define SY__OPL_(S)   SY__GLUE(syopl__, S##_marker)

#define SY__FIRST(S) SY__FIRST_(S)
#define SY__OPL(S)   SY__OPL_(S)

// I dropped this here so that if you copy paste any of the
// above and forget to replace Typezz, you'll get auto-int like
// behavior from your error messages.
#define Typezz int

#endif //SY__H
