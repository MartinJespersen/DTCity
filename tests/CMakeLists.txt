add_executable(city_tests)
list(APPEND ALL_TARGETS city_tests)
set(ALL_TARGETS "${ALL_TARGETS}" PARENT_SCOPE)

target_sources(city_tests
    PRIVATE
    test_main.cpp
    ../src/third_party/simdjson/simdjson.cpp
    ../simulator/third_party/sqlite/sqlite3.c
    ../simulator/third_party/yyjson/yyjson.c
    base/layer_linkage.cpp
)

target_include_directories(city_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(city_tests PRIVATE BUILD_TEST=1 DTCITY_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")

target_link_libraries(city_tests PRIVATE glm::glm)
target_link_libraries(city_tests PRIVATE dtcity_mimalloc)
if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(city_tests PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

enable_testing()
add_test(NAME city_tests COMMAND city_tests)

add_executable(utm_tests utility/utm_test_main.cpp)
target_include_directories(utm_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME utm_tests COMMAND utm_tests)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ASAN_ENABLED)
  # Exercise the real OS entry point and fatal-signal path in isolated children.
  add_executable(linux_crash_probe os_core/linux_crash_probe.cpp)
  target_include_directories(linux_crash_probe PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(linux_crash_probe PRIVATE glm::glm dtcity_mimalloc Threads::Threads ${CMAKE_DL_LIBS} m)
  target_compile_options(linux_crash_probe PRIVATE -g)
  find_program(DTCITY_TEST_LLVM_SYMBOLIZER NAMES llvm-symbolizer)
  foreach(mode main worker abort segv overflow safe_call no_symbolizer quoted_path stalled_symbolizer)
    add_test(NAME linux_crash_${mode}
      COMMAND ${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:linux_crash_probe> -DMODE=${mode}
              -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR} -DSYMBOLIZER_AVAILABLE=${DTCITY_TEST_LLVM_SYMBOLIZER}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/os_core/linux_crash_report.cmake)
  endforeach()
endif()

if(ASAN_ENABLED)
  add_executable(mimalloc_asan_probe base/mimalloc_asan_probe.cpp)
  target_link_libraries(mimalloc_asan_probe PRIVATE dtcity_mimalloc)
  add_test(NAME mimalloc_asan_allocations COMMAND mimalloc_asan_probe allocations)
  foreach(mode overflow use_after_free)
    add_test(NAME mimalloc_asan_${mode}
      COMMAND ${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:mimalloc_asan_probe> -DMODE=${mode}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/base/mimalloc_asan_failure.cmake)
  endforeach()
endif()
