#if !defined(SY__INIT_H)
#define SY__INIT_H

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~~~~ SY Init Definitions ~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

typedef void SY_InitFunc(void);
typedef struct SY_InitRecord{
  SY_InitFunc *func;
} SY_InitRecord;

#define SYMBOL_SET_DEFINE SY_INIT
#define SY_INIT_Type    SY_InitRecord
#define SY_INIT_elf_section    ".sy.init"
#define SY_INIT_coff_a_section ".sy$init_a"
#define SY_INIT_coff_m_section ".sy$init_m"
#define SY_INIT_coff_z_section ".sy$init_z"
#define SY_INIT_marker  init
#include "symbol_set.define.h"

#define SY_INIT(N) \
static void syinit__##N(void); \
SyDefine(SY_INIT, N) = { syinit__##N }; \
static void syinit__##N(void)

void sy__run_init(void);

#endif //SY__INIT_H

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~~~~~~ Function Definitions ~~~~~~~~~~~~~~~~~~~~~~~~~*/

#if SY__MAIN && !defined(SY__INIT_DEFINITIONS)
#define SY__INIT_DEFINITIONS 1

void sy__run_init(void){
  for (SyEach(SY_INIT, init)){
    init->func();
  }
}

#endif //SY__INIT_DEFINITIONS

