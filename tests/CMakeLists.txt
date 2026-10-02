add_executable(city_tests)
list(APPEND ALL_TARGETS city_tests)
set(ALL_TARGETS "${ALL_TARGETS}" PARENT_SCOPE)

target_sources(city_tests
    PRIVATE
    test_main.cpp
    ../src/third_party/simdjson/simdjson.cpp
    ../simulator/third_party/sqlite/sqlite3.c
    ../simulator/third_party/yyjson/yyjson.c
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
