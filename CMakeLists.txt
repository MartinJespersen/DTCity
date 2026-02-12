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

# Third-party dependencies for cesium-native (from vcpkg)
find_package(Async++ CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(draco CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(WebP CONFIG REQUIRED)
find_package(libjpeg-turbo CONFIG REQUIRED)
find_package(ada CONFIG REQUIRED)
find_package(s2 CONFIG REQUIRED)
# modp-base64 has no CMake config, create imported target
add_library(modp_base64 STATIC IMPORTED)
find_library(MODP_BASE64_LIBRARY_RELEASE NAMES libmodpbase64 PATHS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib" NO_DEFAULT_PATH)
find_library(MODP_BASE64_LIBRARY_DEBUG NAMES libmodpbase64 PATHS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib" NO_DEFAULT_PATH)
set_target_properties(modp_base64 PROPERTIES
    IMPORTED_LOCATION "${MODP_BASE64_LIBRARY_RELEASE}"
    IMPORTED_LOCATION_DEBUG "${MODP_BASE64_LIBRARY_DEBUG}")

find_package(spz CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(tinyxml2 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(absl CONFIG REQUIRED)

# Cesium Native - must be built separately
# Set CESIUM_NATIVE_DIR to point to your cesium-native build directory
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(CesiumNative)

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


# Include cesium headers BEFORE other includes to avoid dependency conflicts
target_include_directories(city SYSTEM BEFORE PRIVATE ${CESIUM_NATIVE_INCLUDE_DIR})

# Link Cesium Native libraries
target_link_libraries(city PRIVATE CESIUM_NATIVE::CESIUM_NATIVE)

# Link third-party dependencies for cesium-native (from vcpkg)
target_link_libraries(city PRIVATE
    Async++
    fmt::fmt
    spdlog::spdlog
    CURL::libcurl
    draco::draco
    meshoptimizer::meshoptimizer
    WebP::webp
    WebP::webpdecoder
    WebP::sharpyuv
    libjpeg-turbo::jpeg-static
    libjpeg-turbo::turbojpeg-static
    ada::ada
    s2::s2
    modp_base64
    ZLIB::ZLIB
    zstd::libzstd
    unofficial::sqlite3::sqlite3
    tinyxml2::tinyxml2
    OpenSSL::SSL
    OpenSSL::Crypto
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
    spz::spz
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
