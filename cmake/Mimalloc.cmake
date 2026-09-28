include_guard(GLOBAL)

add_library(dtcity_mimalloc INTERFACE)
if(ASAN_ENABLED)
  if(MSVC)
    # CMake's default Debug runtime checks cannot be combined with ASan.
    foreach(flags CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG)
      string(REPLACE "/RTC1" "" ${flags} "${${flags}}")
    endforeach()
  endif()
  # Build the same pinned version with tracking; the vcpkg binary has no ASan hooks.
  if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
  endif()
  include(FetchContent)
  FetchContent_Declare(dtcity_mimalloc_source
    URL https://codeload.github.com/microsoft/mimalloc/tar.gz/refs/tags/v2.2.6
    URL_HASH SHA256=f2d59a2e5e8ddffb6f98484c39e4ef7ba22abb4c20e59cfdd387c897ac2f5b28
    PATCH_COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=<SOURCE_DIR>
                  -P ${CMAKE_CURRENT_LIST_DIR}/MimallocAsanPatch.cmake
    SOURCE_SUBDIR dtcity-unused
  )
  FetchContent_MakeAvailable(dtcity_mimalloc_source)

  # Use upstream's single-file build, with compiler-specific sanitizer flags.
  # Do not define MI_MALLOC_OVERRIDE: ASan keeps ownership of CRT allocations.
  set(mi_source "${dtcity_mimalloc_source_SOURCE_DIR}/src/static.c")
  add_library(dtcity_mimalloc_asan STATIC "${mi_source}")
  if(MSVC)
    set_source_files_properties("${mi_source}" PROPERTIES LANGUAGE CXX)
    target_compile_options(dtcity_mimalloc_asan PRIVATE /fsanitize=address /Z7)
    target_compile_options(dtcity_mimalloc INTERFACE /fsanitize=address)
    target_link_options(dtcity_mimalloc INTERFACE /INCREMENTAL:NO)
  else()
    target_compile_options(dtcity_mimalloc_asan PRIVATE -fsanitize=address -g)
    target_compile_options(dtcity_mimalloc INTERFACE -fsanitize=address)
    target_link_options(dtcity_mimalloc INTERFACE -fsanitize=address)
  endif()
  target_compile_definitions(dtcity_mimalloc_asan PRIVATE MI_TRACK_ASAN=1 MI_PADDING=1 MI_WIN_NOREDIRECT=1)
  target_include_directories(dtcity_mimalloc_asan SYSTEM PUBLIC "${dtcity_mimalloc_source_SOURCE_DIR}/include")
  if(WIN32)
    target_link_libraries(dtcity_mimalloc_asan PRIVATE psapi shell32 user32 advapi32 bcrypt)
  else()
    find_package(Threads REQUIRED)
    target_link_libraries(dtcity_mimalloc_asan PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      target_link_libraries(dtcity_mimalloc_asan PRIVATE rt atomic)
    endif()
  endif()
  target_compile_definitions(dtcity_mimalloc INTERFACE ASAN_ENABLED=1 DTCITY_MIMALLOC_ASAN=1)
  target_link_libraries(dtcity_mimalloc INTERFACE dtcity_mimalloc_asan)
else()
  find_package(mimalloc CONFIG QUIET)
  if(NOT mimalloc_FOUND)
    # Simulator-only presets do not require a vcpkg toolchain.
    # Build the dependency locally when no installed package is available.
    if(POLICY CMP0135)
      cmake_policy(SET CMP0135 NEW)
    endif()
    include(FetchContent)
    set(MI_BUILD_SHARED OFF)
    set(MI_BUILD_STATIC ON)
    set(MI_BUILD_OBJECT OFF)
    set(MI_BUILD_TESTS OFF)
    set(MI_OVERRIDE OFF)
    set(MI_OSX_INTERPOSE OFF)
    set(MI_OSX_ZONE OFF)
    FetchContent_Declare(dtcity_mimalloc_release_source
      URL https://codeload.github.com/microsoft/mimalloc/tar.gz/refs/tags/v2.2.6
      URL_HASH SHA256=f2d59a2e5e8ddffb6f98484c39e4ef7ba22abb4c20e59cfdd387c897ac2f5b28
    )
    FetchContent_MakeAvailable(dtcity_mimalloc_release_source)
  endif()
  target_link_libraries(dtcity_mimalloc INTERFACE $<IF:$<TARGET_EXISTS:mimalloc-static>,mimalloc-static,mimalloc>)
endif()
