cmake_minimum_required(VERSION 3.20)
project(DTCity)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(THIRD_PARTY_LIBS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/third_party)
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data/shaders)
set(SHADER_BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/bin)
set(ASAN_LINK_FLAGS "")
set(ASAN_COMPILE_FLAGS "")
set(ALL_TARGETS city)

option(BUILD_DEBUG "Enable Additional Debugging Information (e.g Vulkan Validation Layers)" OFF)
option(ASAN_ENABLED "Enable Address Sanitizer" OFF)

# cmake macro -> compiler macro
if(BUILD_DEBUG)
    add_compile_definitions(BUILD_DEBUG=1)
endif()

if(ASAN_ENABLED)
    add_compile_definitions(ASAN_ENABLED=1)
endif()

add_executable(city)

# Compiler specific options
if (MSVC)
    if(BUILD_DEBUG)
        message(DEBUG "Setting MSVC flags to /Z7 for ccache compatibility.  Current flags: ${CMAKE_CXX_FLAGS_DEBUG}")
        string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
        string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
        string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
        message(DEBUG "New flags: ${CMAKE_CXX_FLAGS_DEBUG}")

        add_compile_options(
   /Z7
  )
    endif()
    target_compile_options(city PRIVATE
        /W4
        /wd4201  /wd4005 /wd4838 /wd4244 /wd4996 /wd4310 /wd4245 /wd4100 /wd4505
        /EHsc
        /utf-8
        /Zc:__cplusplus
    )

    add_compile_definitions(BUILD_CONSOLE_INTERFACE ON)

    list(APPEND ASAN_LINK_FLAGS /INCREMENTAL:NO)
    list(APPEND ASAN_COMPILE_FLAGS /fsanitize=address /D_DISABLE_STRING_ANNOTATION /D_DISABLE_VECTOR_ANNOTATION)
elseif(CMAKE_COMPILER_IS_GNUCXX)
    target_compile_options(city PRIVATE
    -maes
    -msse4
    )
    if(BUILD_DEBUG AND NOT NDEBUG)
        target_compile_options(city PRIVATE
        -Wall -Wextra -Wno-attributes -Wno-format -Wno-unused-function -Wno-unused-parameter -Wno-narrowing -Wno-missing-field-initializers -Wno-sign-conversion -Wno-write-strings -Wno-class-memaccess -Wno-comment -Wno-pedantic
    )

    else()

        target_compile_options(city PRIVATE -w)
    endif()

    list(APPEND ASAN_LINK_FLAGS -fsanitize=address)
    list(APPEND ASAN_COMPILE_FLAGS -fsanitize=address)
else()
    message(FATAL_ERROR "Unsupported compiler")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/third_party/imgui)


# address sanitizer
if(ASAN_ENABLED)
    foreach(target ${ALL_TARGETS})
        target_compile_options(${target} PRIVATE ${ASAN_COMPILE_FLAGS})
        target_link_options(${target} PRIVATE ${ASAN_LINK_FLAGS})
    endforeach()
endif()


# tracy profiler
option(TRACY_PROFILE_ENABLE "Enable Tracy profiler" OFF)
if (TRACY_PROFILE_ENABLE)
    add_compile_definitions(TRACY_ENABLE)
    add_compile_definitions(TRACY_PROFILE_ENABLE)
    target_sources(city PRIVATE ${THIRD_PARTY_LIBS_DIR}/tracy/TracyClient.cpp)
endif()

# compile shaders
file(GLOB_RECURSE GLSL_SOURCE_FILES
    "${SHADER_DIR}/*.vert"
    "${SHADER_DIR}/*.frag"
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
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BINARY_DIR}"
        COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
        DEPENDS ${GLSL}
        COMMENT "Compiling ${FILE_NAME} to SPIR-V"
    )
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach()
add_custom_target(CompileShaders ALL DEPENDS ${SPIRV_BINARY_FILES})

target_sources(city
    PRIVATE
    src/
    src/main.cpp
)

target_include_directories(city SYSTEM PRIVATE
    ${THIRD_PARTY_LIBS_DIR}
)

# KTX from vcpkg (with Vulkan support) - cesium-native's KTX doesn't include ktxvulkan.h
find_package(Ktx CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(Vulkan REQUIRED)

# Cesium Native (from vcpkg overlay port)
find_package(cesium-native CONFIG REQUIRED)

find_package(absl CONFIG REQUIRED)

# city executable specifics
add_dependencies(city CompileShaders)

target_link_directories(city PRIVATE
        ${PLATFORM_LIB_ARTIFACT_DIR}
)
target_sources(city PRIVATE src/third_party/simdjson/simdjson.cpp src/third_party/imgui/imgui_inc.cpp)
target_link_libraries(city PRIVATE
    glfw
    KTX::ktx
    Vulkan::Vulkan
)


# Link Cesium Native libraries (transitive dependencies resolved via vcpkg config)
target_link_libraries(city PRIVATE
    Cesium3DTilesSelection
    Cesium3DTilesContent
    Cesium3DTilesReader
    Cesium3DTiles
    CesiumGltfContent
    CesiumGltfReader
    CesiumGltf
    CesiumRasterOverlays
    CesiumQuantizedMeshTerrain
    CesiumGeospatial
    CesiumGeometry
    CesiumAsync
    CesiumJsonReader
    CesiumUtility
    CesiumCurl
    Cesium3DTilesWriter
    CesiumGltfWriter
    CesiumJsonWriter
    CesiumIonClient
    CesiumITwinClient
    CesiumClientCommon
    CesiumVectorData
    absl::base
    absl::strings
    absl::status
    absl::statusor
    absl::hash
    absl::time
    absl::synchronization
    absl::log
    absl::log_internal_check_op
    absl::log_internal_message
    absl::log_internal_nullguard
    absl::log_internal_format
    absl::log_internal_globals
    absl::log_internal_log_sink_set
    absl::raw_logging_internal
    absl::cord
    absl::int128
    absl::str_format
)

# Cesium-native requires some additional compile definitions
target_compile_definitions(city PRIVATE
        SPDLOG_COMPILED_LIB
        GLM_FORCE_XYZW_ONLY
        GLM_FORCE_EXPLICIT_CTOR
        GLM_FORCE_SIZE_T_LENGTH
        LIBASYNC_STATIC
    )

add_custom_target(clang-tidy-check
    COMMAND clang-tidy -p ${CMAKE_BINARY_DIR} "src/main.cpp"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running clang-tidy on"
)
