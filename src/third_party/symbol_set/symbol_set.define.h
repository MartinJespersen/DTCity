#if SY__OS_LINUX

extern SyType(SYMBOL_SET_DEFINE) SY__FIRST(SYMBOL_SET_DEFINE);
extern SyType(SYMBOL_SET_DEFINE) SY__OPL(SYMBOL_SET_DEFINE);
// (these symbols are resolved in the linker script)

#elif SY__OS_WINDOWS

#if SY__COMPILER_CL
# pragma section(SY__GLUE(SYMBOL_SET_DEFINE,_coff_a_section),read,write)
# pragma section(SY__GLUE(SYMBOL_SET_DEFINE,_coff_m_section),read,write)
# pragma section(SY__GLUE(SYMBOL_SET_DEFINE,_coff_z_section),read,write)
#endif

#if defined(SY__MAIN)

SY__ALIGN_AS_LIT(8)
SY__SEC_RW(SY__GLUE(SYMBOL_SET_DEFINE,_coff_a_section))
SyType(SYMBOL_SET_DEFINE) SY__FIRST(SYMBOL_SET_DEFINE) = {
#if !defined(__cplusplus)
  0
#endif
};

SY__ALIGN_AS_LIT(8)
SY__SEC_RW(SY__GLUE(SYMBOL_SET_DEFINE,_coff_z_section))
SyType(SYMBOL_SET_DEFINE) SY__OPL(SYMBOL_SET_DEFINE) = {
#if !defined(__cplusplus)
  0
#endif
};

#else

extern SyType(SYMBOL_SET_DEFINE) SY__FIRST(SYMBOL_SET_DEFINE);
extern SyType(SYMBOL_SET_DEFINE) SY__OPL(SYMBOL_SET_DEFINE);

#endif

#else
# error symbol_set.define not implemented for this OS
#endif

#undef SYMBOL_SET_DEFINE
