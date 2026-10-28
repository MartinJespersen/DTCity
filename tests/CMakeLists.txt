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

find_package(CURL REQUIRED)
target_link_libraries(city_tests PRIVATE glm::glm CURL::libcurl)
target_link_libraries(city_tests PRIVATE dtcity_mimalloc)
if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(city_tests PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

add_test(NAME city_tests COMMAND city_tests)

# Keep arena instrumentation covered even when the main test build disables debug logging.
add_executable(arena_debug_tests base/test_arena_debug.cpp)
target_include_directories(arena_debug_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(arena_debug_tests PRIVATE BUILD_TEST=1 BUILD_DEBUG=1)
target_link_libraries(arena_debug_tests PRIVATE glm::glm dtcity_mimalloc)
if(NOT WIN32)
  target_link_libraries(arena_debug_tests PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()
list(APPEND ALL_TARGETS arena_debug_tests)
set(ALL_TARGETS "${ALL_TARGETS}" PARENT_SCOPE)
add_test(NAME arena_debug COMMAND arena_debug_tests)

# Test commit failures without consuming the machine's available memory.
add_executable(arena_failure_tests base/test_arena_failure.cpp)
target_include_directories(arena_failure_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(arena_failure_tests PRIVATE BUILD_TEST=1)
target_link_libraries(arena_failure_tests PRIVATE glm::glm dtcity_mimalloc)
add_test(NAME arena_failure COMMAND arena_failure_tests)

add_executable(call_stack_probe os_core/call_stack_probe.cpp)
target_include_directories(call_stack_probe PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(call_stack_probe PRIVATE BUILD_CONSOLE_INTERFACE=1)
target_link_libraries(call_stack_probe PRIVATE glm::glm dtcity_mimalloc)
if(NOT WIN32)
  target_link_libraries(call_stack_probe PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
  target_compile_options(call_stack_probe PRIVATE -g)
endif()
add_test(NAME call_stack_report
  COMMAND ${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:call_stack_probe>
          -P ${CMAKE_CURRENT_SOURCE_DIR}/os_core/call_stack_report.cmake)

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

add_executable(mimalloc_counter_probe base/mimalloc_counter_probe.cpp)
target_link_libraries(mimalloc_counter_probe PRIVATE dtcity_mimalloc)
add_test(NAME mimalloc_counter COMMAND mimalloc_counter_probe)
set_tests_properties(mimalloc_counter PROPERTIES TIMEOUT 30)

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

# Execute the production LOD shader without a window. Software Vulkan also works.
find_package(Vulkan QUIET)
if(Vulkan_FOUND AND Vulkan_GLSLANG_VALIDATOR_EXECUTABLE)
  set(TILE_HEIGHT_SHADER "${CMAKE_CURRENT_BINARY_DIR}/tile_height_comp.spv")
  add_custom_command(OUTPUT "${TILE_HEIGHT_SHADER}"
    COMMAND "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}" -V --target-env vulkan1.2
            "${CMAKE_SOURCE_DIR}/src/shaders/tile_height.comp" -o "${TILE_HEIGHT_SHADER}"
    DEPENDS "${CMAKE_SOURCE_DIR}/src/shaders/tile_height.comp" "${CMAKE_SOURCE_DIR}/src/shaders/shader_shared.h")
  add_executable(tile_height_compute_test render/test_tile_height_compute.cpp
    ../src/render/vulkan/tile_height_compute.cpp "${TILE_HEIGHT_SHADER}")
  # clangd also infers neighboring Vulkan app files from this shared source.
  target_include_directories(tile_height_compute_test PRIVATE "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/third_party" "${CMAKE_SOURCE_DIR}/src/third_party/imgui")
  target_link_libraries(tile_height_compute_test PRIVATE Vulkan::Vulkan glm::glm)
  list(APPEND ALL_TARGETS tile_height_compute_test)
  set(ALL_TARGETS "${ALL_TARGETS}" PARENT_SCOPE)
  add_test(NAME tile_height_compute COMMAND tile_height_compute_test "${TILE_HEIGHT_SHADER}")
  set_tests_properties(tile_height_compute PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
  set(MODEL_LOD_SHADER "${CMAKE_CURRENT_BINARY_DIR}/model_lod_comp.spv")
  add_custom_command(OUTPUT "${MODEL_LOD_SHADER}"
    COMMAND "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}" -V
            "${CMAKE_SOURCE_DIR}/src/shaders/model_instance/model_lod.comp" -o "${MODEL_LOD_SHADER}"
    DEPENDS "${CMAKE_SOURCE_DIR}/src/shaders/model_instance/model_lod.comp"
            "${CMAKE_SOURCE_DIR}/src/shaders/model_instance/lod_select.glsl"
            "${CMAKE_SOURCE_DIR}/src/shaders/shader_shared.h")
  add_executable(model_lod_compute_test render/test_model_lod_compute.cpp
    ../src/render/vulkan/synchronization.cpp "${MODEL_LOD_SHADER}")
  target_include_directories(model_lod_compute_test PRIVATE "${CMAKE_SOURCE_DIR}/src")
  target_link_libraries(model_lod_compute_test PRIVATE Vulkan::Vulkan glm::glm)
  list(APPEND ALL_TARGETS model_lod_compute_test)
  set(ALL_TARGETS "${ALL_TARGETS}" PARENT_SCOPE)
  add_test(NAME model_lod_compute COMMAND model_lod_compute_test "${MODEL_LOD_SHADER}")
  set_tests_properties(model_lod_compute PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Exercise acquire/render/present synchronization with a real swapchain.
if(Vulkan_FOUND)
  find_package(glfw3 CONFIG QUIET)
  if(TARGET glfw)
    add_executable(swapchain_sync_test render/test_swapchain_sync.cpp
      ../src/render/vulkan/synchronization.cpp)
    target_include_directories(swapchain_sync_test PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(swapchain_sync_test PRIVATE Vulkan::Vulkan glfw)
    list(APPEND ALL_TARGETS swapchain_sync_test)
    set(ALL_TARGETS "${ALL_TARGETS}" PARENT_SCOPE)
    add_test(NAME swapchain_sync COMMAND swapchain_sync_test)
    set_tests_properties(swapchain_sync PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
  endif()
endif()

# OSM parsing uses the application's Cesium dependencies.
if(BUILD_CITY)
  find_package(cesium-native CONFIG REQUIRED)
  add_executable(network_load_tests osm/test_network_load.cpp ../src/third_party/simdjson/simdjson.cpp)
  target_include_directories(network_load_tests PRIVATE ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/third_party ${CMAKE_SOURCE_DIR}/src/third_party/imgui)
  target_compile_definitions(network_load_tests PRIVATE BUILD_TEST=1 GLM_FORCE_XYZW_ONLY GLM_FORCE_EXPLICIT_CTOR)
  target_link_libraries(network_load_tests PRIVATE Cesium3DTilesSelection CURL::libcurl dtcity_mimalloc)
  if(NOT WIN32)
    target_link_libraries(network_load_tests PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
  endif()
  list(APPEND ALL_TARGETS network_load_tests)
  set(ALL_TARGETS "${ALL_TARGETS}" PARENT_SCOPE)
  add_test(NAME network_load COMMAND network_load_tests)
endif()
