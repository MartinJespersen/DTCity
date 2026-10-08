cmake_minimum_required(VERSION 3.20)
project(DTCity)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(THIRD_PARTY_LIBS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/third_party)
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data/shaders)
set(SHADER_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders/bin)
set(ASAN_LINK_FLAGS "")
set(ASAN_COMPILE_FLAGS "")
set(GLSL_SHADER_FLAGS "")
set(DISABLE_WARNINGS_COMPILE_FLAG "")
set(ALL_TARGETS)
set(RADLINKER_PATH "C:/repos/raddebugger/build/radlink.exe")

option(BUILD_DEBUG "Enable Additional Debugging Information (e.g Vulkan Validation Layers)" OFF)
option(ASAN_ENABLED "Enable Address Sanitizer" OFF)
option(CITY_MONOLITHIC_BUILD "Compile all city layers in one object for build-time comparisons" OFF)
option(BUILD_CITY "Build the DTCity application" ON)
option(BUILD_SIMULATOR "Build the vehicle simulator" OFF)
option(BUILD_TESTS "Build tests" OFF)
option(SHADER_DEBUG "Enable Shader Debugging" OFF)
option(USE_RADLINKER "Use RAD linker for MSVC debug builds" ON)


# cmake macro -> compiler macro
if(BUILD_DEBUG AND SHADER_DEBUG)
  add_compile_definitions(SHADER_DEBUG=1)
  list(APPEND GLSL_SHADER_FLAGS -gVS)
endif()

if(BUILD_DEBUG)
  add_compile_definitions(BUILD_DEBUG=1)
endif()

if(ASAN_ENABLED)
  add_compile_definitions(ASAN_ENABLED=1)
endif()


# Compiler specific options
if (MSVC)
  if(ASAN_ENABLED)
    find_program(DTCITY_MSVC_LINKER_PATH NAMES link REQUIRED)
    set(DTCITY_LINKER_PATH "${DTCITY_MSVC_LINKER_PATH}")
    message(STATUS "Using Microsoft linker for AddressSanitizer: ${DTCITY_LINKER_PATH}")
  elseif(USE_RADLINKER)
    if(EXISTS "${RADLINKER_PATH}")
      set(DTCITY_LINKER_PATH "${RADLINKER_PATH}")
      message(STATUS "Using RAD linker: ${DTCITY_LINKER_PATH}")
    else()
      message(FATAL_ERROR "RAD linker not found at ${RADLINKER_PATH}")
    endif()
  else()
    find_program(DTCITY_LLD_LINKER_PATH NAMES lld-link REQUIRED)
    set(DTCITY_LINKER_PATH "${DTCITY_LLD_LINKER_PATH}")
    message(STATUS "Using LLVM linker: ${DTCITY_LINKER_PATH}")
  endif()

  set(CMAKE_LINKER "${DTCITY_LINKER_PATH}" CACHE FILEPATH "Linker used for MSVC object files" FORCE)
  set(CMAKE_CXX_LINK_EXECUTABLE
      "<CMAKE_LINKER> /nologo <OBJECTS> ${CMAKE_START_TEMP_FILE} /out:<TARGET> /implib:<TARGET_IMPLIB> /pdb:<TARGET_PDB> /version:<TARGET_VERSION_MAJOR>.<TARGET_VERSION_MINOR> <LINK_FLAGS> <LINK_LIBRARIES> ${CMAKE_END_TEMP_FILE}")
  set(CMAKE_C_LINK_EXECUTABLE "${CMAKE_CXX_LINK_EXECUTABLE}")

  message(DEBUG "Setting MSVC flags to /Z7 for ccache compatibility. Current flags: ${CMAKE_CXX_FLAGS_DEBUG}")
  string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
  string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
  string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
  string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
  message(DEBUG "New flags: ${CMAKE_CXX_FLAGS_DEBUG}")

  add_compile_options(
            /W4
            /wd4201  /wd4005 /wd4838 /wd4244 /wd4996 /wd4310 /wd4245 /wd4100
            /EHsc
            /utf-8
            /Zc:preprocessor
            /Zc:__cplusplus
        )

  add_compile_definitions(BUILD_CONSOLE_INTERFACE=1)

  list(APPEND ASAN_LINK_FLAGS /INCREMENTAL:NO)
  list(APPEND ASAN_COMPILE_FLAGS /fsanitize=address /D_DISABLE_STRING_ANNOTATION /D_DISABLE_VECTOR_ANNOTATION /D_DISABLE_OPTIONAL_ANNOTATION)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(
    -maes
    -msse4
    )
  if(BUILD_DEBUG AND NOT NDEBUG)
    add_compile_options(
        -Wall -Wextra -Wno-attributes -Wno-format -Wno-unused-function -Wno-unused-parameter -Wno-narrowing -Wno-missing-field-initializers -Wno-sign-conversion -Wno-write-strings -Wno-comment -Wno-pedantic
    )

    add_link_options(-fuse-ld=lld)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      add_compile_options(-Wno-class-memaccess)
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      add_compile_options(-ftime-trace)
    endif()

  else()

    add_compile_options(-w)
  endif()


  list(APPEND ASAN_LINK_FLAGS -fsanitize=address)
  list(APPEND ASAN_COMPILE_FLAGS -fsanitize=address)
  set(DISABLE_WARNINGS_COMPILE_FLAG -w)

  if(WIN32)
    add_compile_definitions(BUILD_CONSOLE_INTERFACE=1)
    list(APPEND ASAN_COMPILE_FLAGS -D_DISABLE_STRING_ANNOTATION -D_DISABLE_VECTOR_ANNOTATION -D_DISABLE_OPTIONAL_ANNOTATION)
  endif()
else()
  message(FATAL_ERROR "Unsupported compiler")
endif()

function(disable_warnings_for_sources)
  set_source_files_properties(
    ${ARGV}
    PROPERTIES
    COMPILE_OPTIONS "${DISABLE_WARNINGS_COMPILE_FLAG}"
  )
endfunction()

set(THIRD_PARTY_SOURCES
    src/third_party/simdjson/simdjson.cpp
    src/third_party/imgui/imgui_inc.cpp
    src/third_party/vma_stb.cpp
)

option(TRACY_PROFILE_ENABLE "Enable Tracy profiler" OFF)
if(TRACY_PROFILE_ENABLE)
  list(APPEND THIRD_PARTY_SOURCES ${THIRD_PARTY_LIBS_DIR}/tracy/TracyClient.cpp)
endif()

if(BUILD_CITY OR BUILD_TESTS)
  find_package(glm CONFIG REQUIRED)
endif()

if(BUILD_CITY OR BUILD_TESTS OR BUILD_SIMULATOR)
  include(cmake/Mimalloc.cmake)
endif()

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(BUILD_CITY)
  find_package(meshoptimizer CONFIG REQUIRED)
  add_executable(city)
  # Use simdjson's explicit error-code API in both the application and library sources.
  target_compile_definitions(city PRIVATE SIMDJSON_EXCEPTIONS=0
      DTCITY_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")
  target_link_libraries(city PRIVATE meshoptimizer::meshoptimizer)
  target_link_libraries(city PRIVATE dtcity_mimalloc)
  list(APPEND ALL_TARGETS city)

  include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/third_party/imgui)

# compile shaders
file(GLOB_RECURSE GLSL_SOURCE_FILES CONFIGURE_DEPENDS
    "${SHADER_DIR}/*.vert"
    "${SHADER_DIR}/*.frag"
    "${SHADER_DIR}/*.comp"
    "${SHADER_DIR}/*.tesc"
    "${SHADER_DIR}/*.tese"
    "${SHADER_DIR}/*.mesh"
    "${SHADER_DIR}/*.task"
)


find_program(GLSL_VALIDATOR glslangValidator)
if (NOT GLSL_VALIDATOR)
  message(FATAL_ERROR "glslangValidator not found!")
endif()

foreach(GLSL ${GLSL_SOURCE_FILES})
  get_filename_component(FILE_NAME ${GLSL} NAME_WE)
  get_filename_component(EXT_WITH_DOT ${GLSL} EXT)
  string(SUBSTRING ${EXT_WITH_DOT} 1 -1 EXT)
  set(SPIRV "${SHADER_BINARY_DIR}/${FILE_NAME}_${EXT}.spv")
  set(SHADER_TARGET_FLAGS "")
  if(EXT STREQUAL "mesh" OR EXT STREQUAL "task")
    list(APPEND SHADER_TARGET_FLAGS --target-env vulkan1.2)
  endif()
  add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BINARY_DIR}"
        COMMAND ${GLSL_VALIDATOR} -V ${GLSL} ${GLSL_SHADER_FLAGS} ${SHADER_TARGET_FLAGS} -o ${SPIRV}
        DEPENDS ${GLSL}
        COMMENT "Compiling ${FILE_NAME} to SPIR-V"
    )
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach()
add_custom_target(CompileShaders ALL DEPENDS ${SPIRV_BINARY_FILES})

# Keep unity compilation within each layer, with independent incremental objects.
# A monolithic mode is retained for timing comparisons on the same toolchain.
set(CITY_LAYER_SOURCES
    # Start the larger units first so they do not trail the parallel build.
    src/build_units/cesium.cpp
    src/build_units/city.cpp
    src/build_units/render.cpp
    src/build_units/base.cpp
    src/build_units/osm.cpp
    src/build_units/app.cpp
    src/build_units/async.cpp
    src/build_units/gltf.cpp
    src/build_units/misc.cpp
    src/build_units/json.cpp
    src/build_units/draw.cpp
    src/build_units/utility.cpp
)
if(CITY_MONOLITHIC_BUILD)
  set(CITY_BUILD_SOURCES src/build_units/unity.cpp)
else()
  set(CITY_BUILD_SOURCES ${CITY_LAYER_SOURCES})
endif()
target_sources(city PRIVATE ${CITY_BUILD_SOURCES})
set_target_properties(city PROPERTIES UNITY_BUILD OFF)

target_include_directories(city SYSTEM PRIVATE
    ${THIRD_PARTY_LIBS_DIR}
)

# Enable interprocedural optimization on the existing application target.
set_property(TARGET city PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

target_precompile_headers(city PRIVATE src/pch.hpp)

# KTX from vcpkg (with Vulkan support) - cesium-native's KTX doesn't include ktxvulkan.h
find_package(Ktx CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(Vulkan REQUIRED)

# Cesium Native (from vcpkg overlay port)
find_package(cesium-native CONFIG REQUIRED)

find_package(absl CONFIG REQUIRED)

# Async++ exports an MSVC-style option for every Windows compiler. The GNU-style
# Clang driver treats it as an input path, so remove it from the imported target.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
  get_target_property(ASYNC_COMPILE_OPTIONS Async++ INTERFACE_COMPILE_OPTIONS)
  if(ASYNC_COMPILE_OPTIONS)
    list(REMOVE_ITEM ASYNC_COMPILE_OPTIONS "/Zc:__cplusplus")
    set_target_properties(Async++ PROPERTIES INTERFACE_COMPILE_OPTIONS "${ASYNC_COMPILE_OPTIONS}")
  endif()

  get_property(IMPORTED_TARGETS DIRECTORY PROPERTY IMPORTED_TARGETS)
  foreach(IMPORTED_TARGET IN LISTS IMPORTED_TARGETS)
    if(IMPORTED_TARGET MATCHES "^absl::")
      get_target_property(IMPORTED_LINK_LIBRARIES ${IMPORTED_TARGET} INTERFACE_LINK_LIBRARIES)
      if(IMPORTED_LINK_LIBRARIES)
        list(REMOVE_ITEM IMPORTED_LINK_LIBRARIES "-ignore:4221" "$<LINK_ONLY:-ignore:4221>")
        set_target_properties(${IMPORTED_TARGET} PROPERTIES INTERFACE_LINK_LIBRARIES "${IMPORTED_LINK_LIBRARIES}")
      endif()
    endif()
  endforeach()
endif()

# city executable specifics
add_dependencies(city CompileShaders)

target_link_directories(city PRIVATE
        ${PLATFORM_LIB_ARTIFACT_DIR}
)
target_sources(city PRIVATE ${THIRD_PARTY_SOURCES})
set_source_files_properties(
    ${THIRD_PARTY_SOURCES}
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)
disable_warnings_for_sources(${THIRD_PARTY_SOURCES})
target_link_libraries(city PRIVATE
    glfw
    KTX::ktx
    Vulkan::Vulkan
    Cesium3DTilesSelection
    CesiumCurl
)

# Cesium-native requires some additional compile definitions
target_compile_definitions(city PRIVATE
        SPDLOG_COMPILED_LIB
        GLM_FORCE_XYZW_ONLY
        GLM_FORCE_EXPLICIT_CTOR
        LIBASYNC_STATIC
    )

if(TRACY_PROFILE_ENABLE)
  target_compile_definitions(city PRIVATE
      TRACY_ENABLE
      TRACY_PROFILE_ENABLE
      TRACY_VULKAN_ENABLE
  )
endif()


add_custom_target(clang-tidy-check
    COMMAND clang-tidy -p ${CMAKE_BINARY_DIR} ${CITY_BUILD_SOURCES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running clang-tidy on city translation units"
    VERBATIM
)
endif()

if(BUILD_SIMULATOR)
  add_subdirectory(simulator)
  list(APPEND ALL_TARGETS Simulator)
endif()

# install
if(BUILD_CITY AND BUILD_SIMULATOR)
  set(DTCITY_PACKAGE_DIR "${CMAKE_BINARY_DIR}/dist" CACHE PATH "Directory for the portable application bundle")
  set(DTCITY_PACKAGE_SIMULATOR_DATABASE "${CMAKE_SOURCE_DIR}/simulator/database/eskiltuna_test.sqlite"
      CACHE FILEPATH "Simulator database to package; empty omits the database")

  install(TARGETS city Simulator RUNTIME DESTINATION . COMPONENT Runtime)
  install(DIRECTORY data/assets data/textures data/fonts DESTINATION data COMPONENT Runtime)
  install(FILES ${SPIRV_BINARY_FILES} DESTINATION data/shaders/bin COMPONENT Runtime)
  install(DIRECTORY simulator/fonts DESTINATION simulator COMPONENT Runtime)
  if(DTCITY_PACKAGE_SIMULATOR_DATABASE)
    if(EXISTS "${DTCITY_PACKAGE_SIMULATOR_DATABASE}")
      install(FILES "${DTCITY_PACKAGE_SIMULATOR_DATABASE}" DESTINATION simulator/database
              RENAME eskiltuna_test.sqlite COMPONENT Runtime)
    else()
      message(STATUS "Packaging without a simulator database: ${DTCITY_PACKAGE_SIMULATOR_DATABASE} does not exist")
    endif()
  endif()

  add_custom_target(package-runtime
    COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}" --config "$<CONFIG>"
            --prefix "${DTCITY_PACKAGE_DIR}" --component Runtime
    DEPENDS city Simulator
    COMMENT "Collecting DTCity, Simulator and runtime data in ${DTCITY_PACKAGE_DIR}"
    VERBATIM
  )
endif()

# address sanitizer
if(ASAN_ENABLED)
  foreach(target ${ALL_TARGETS})
    target_compile_options(${target} PRIVATE ${ASAN_COMPILE_FLAGS})
    target_link_options(${target} PRIVATE ${ASAN_LINK_FLAGS})
  endforeach()
endif()
