cmake_minimum_required(VERSION 3.15)
project(DTCity)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(THIRD_PARTY_LIBS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/third_party)
set(BUILD_DEBUG ON)
set(LIB_ARTIFACT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib_artifacts)

add_executable(city)

if (MSVC)
    target_compile_options(city PRIVATE
        /W4
        /wd4201  /wd4005 /wd4838 /wd4244 /wd4996 /wd4310 /wd4245 /wd4100
        /EHsc
        /Z7
        /FI${CMAKE_SOURCE_DIR}/src/includes.hpp
    )
    add_compile_definitions(BUILD_CONSOLE_INTERFACE ON)

    set(PLATFORM_LIB_ARTIFACT_DIR ${LIB_ARTIFACT_DIR}/win)

elseif(CMAKE_COMPILER_IS_GNUCXX)
    #-Wall -Wextra -pedantic -pedantic -Wno-unused-function  -Wno-missing-field-initializers -Wno-sign-conversion -Wno-write-strings -Wno-class-memaccess -Wno-comment -Wno-pedantic
    target_compile_options(city PRIVATE
    -w
    -maes
    -msse4
    --include ${CMAKE_SOURCE_DIR}/src/includes.hpp
    )


    target_include_directories(city PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui)

    set(PLATFORM_LIB_ARTIFACT_DIR ${LIB_ARTIFACT_DIR}/linux)

else()
    message(FATAL_ERROR "Unsupported compiler")
endif()


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
