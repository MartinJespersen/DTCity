cmake_minimum_required(VERSION 3.15)
project(DTCity)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(THIRD_PARTY_LIBS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/third_party)
set(BUILD_DEBUG ON)
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data/shaders)
set(SHADER_BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/bin)
set(LIB_ARTIFACT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib_artifacts)
set(THIRD_PARTY_TOOLS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tools)

add_executable(city)


if (MSVC)
    target_compile_options(city PRIVATE
        /W4
        /wd4201  /wd4005 /wd4838 /wd4244 /wd4996 /wd4310 /wd4245 /wd4100 /wd4505
        /EHsc
        /Z7
        /FI${CMAKE_SOURCE_DIR}/src/includes.hpp
    )
    add_compile_definitions(BUILD_CONSOLE_INTERFACE ON)

    set(PLATFORM_LIB_ARTIFACT_DIR ${LIB_ARTIFACT_DIR}/win)
    set(PLATFORM_THIRD_PARTY_TOOLS_DIR ${THIRD_PARTY_TOOLS_DIR}/win)

elseif(CMAKE_COMPILER_IS_GNUCXX)
    target_compile_options(city PRIVATE
    -maes
    -msse4
    --include ${CMAKE_SOURCE_DIR}/src/includes.hpp
    )

    if(BUILD_DEBUG AND NOT NDEBUG)
        target_compile_options(city PRIVATE
            -fsanitize=address
            -Wall -Wextra -Wno-attributes -Wno-unused-function -Wno-unused-parameter -Wno-narrowing -Wno-missing-field-initializers -Wno-sign-conversion -Wno-write-strings -Wno-class-memaccess -Wno-comment -Wno-pedantic
        )
        TARGET_LINK_OPTIONS(city PRIVATE
            -fsanitize=address
        )

    else()

        target_compile_options(city PRIVATE -w)
    endif()



    target_include_directories(city PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui)

    set(PLATFORM_LIB_ARTIFACT_DIR ${LIB_ARTIFACT_DIR}/linux/win)
    set(PLATFORM_THIRD_PARTY_TOOLS_DIR ${THIRD_PARTY_TOOLS_DIR}/linux)



else()
    message(FATAL_ERROR "Unsupported compiler")
endif()

option(TRACY_PROFILE_ENABLE "Enable Tracy profiler" OFF)
if (TRACY_PROFILE_ENABLE)
    add_compile_definitions(TRACY_ENABLE)
    target_link_libraries(city PRIVATE TracyClient)
endif()

file(GLOB_RECURSE GLSL_SOURCE_FILES
    "${SHADER_DIR}/*.vert"
    "${SHADER_DIR}/*.frag"
)

find_program(GLSL_VALIDATOR glslangValidator HINTS ${PLATFORM_THIRD_PARTY_TOOLS_DIR})
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
add_dependencies(city CompileShaders)

target_sources(city
    PRIVATE
    src/main.cpp
)
target_include_directories(city
    PRIVATE
        ${THIRD_PARTY_LIBS_DIR}
        ${THIRD_PARTY_LIBS_DIR}/glfw/include
        ${THIRD_PARTY_LIBS_DIR}/ktx/include
        ${THIRD_PARTY_LIBS_DIR}/vulkan_sdk_v1_4_309_0/Include
)


target_link_directories(city
    PRIVATE
        ${PLATFORM_LIB_ARTIFACT_DIR}
)
target_link_libraries(city PRIVATE glfw3 ktx_read vulkan)
