# In 2.2.6, allocation temporarily unpoisons the entire rounded block but only
# unpoisons the requested range afterwards; it never re-poisons the unused tail.
# Restore the tail after mimalloc finishes initializing the block and its padding.
set(path "${SOURCE_DIR}/src/alloc.c")
file(READ "${path}" source)
set(before "  return block;\n}\n\n// extra entries")
set(after "  #if MI_TRACK_ASAN && MI_PADDING\n  mi_track_mem_noaccess((uint8_t*)block + size - MI_PADDING_SIZE,\n                        mi_page_block_size(page) - (size - MI_PADDING_SIZE));\n  #endif\n  return block;\n}\n\n// extra entries")
string(FIND "${source}" "${after}" patched)
if(patched EQUAL -1)
  string(FIND "${source}" "${before}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Mimalloc ASan padding patch no longer matches the pinned source")
  endif()
  string(REPLACE "${before}" "${after}" source "${source}")
  file(WRITE "${path}" "${source}")
endif()
