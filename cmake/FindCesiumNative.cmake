#
# FindCesiumNative.cmake
#
# Finds cesium-native libraries and headers.
#
# Inputs:
#   CESIUM_NATIVE_DIR : cesium-native build directory (e.g., C:/repos/cesium-native/build_asan)
#       - Cesium libs come from: ${CESIUM_NATIVE_DIR}/<lib>/Debug|Release
#       (or "CESIUM_NATIVE_DIR" environment variable)
#   CESIUM_NATIVE_INSTALL_DIR : cesium-native install directory (e.g., C:/cesium-native-install-asan)
#       - Cesium headers come from: ${CESIUM_NATIVE_INSTALL_DIR}/include
#       (or "CESIUM_NATIVE_INSTALL_DIR" environment variable)
#
# Outputs:
#   CESIUM_NATIVE_FOUND (boolean variable)
#   CESIUM_NATIVE::CESIUM_NATIVE (imported link library)
#
# Note: Third-party dependencies (fmt, spdlog, curl, draco, abseil, etc.) should be
#       provided via vcpkg and linked separately in CMakeLists.txt.
#
# Usage:
#   find_package(CesiumNative)
#   ...
#   target_link_libraries(my_target PRIVATE CESIUM_NATIVE::CESIUM_NATIVE)
#

unset(CESIUM_NATIVE_FOUND)

# Determine CESIUM_NATIVE_DIR
if(NOT CESIUM_NATIVE_DIR AND DEFINED ENV{CESIUM_NATIVE_DIR})
    set(CESIUM_NATIVE_DIR "$ENV{CESIUM_NATIVE_DIR}")
endif()

# Determine CESIUM_NATIVE_INSTALL_DIR
if(NOT CESIUM_NATIVE_INSTALL_DIR AND DEFINED ENV{CESIUM_NATIVE_INSTALL_DIR})
    set(CESIUM_NATIVE_INSTALL_DIR "$ENV{CESIUM_NATIVE_INSTALL_DIR}")
endif()

# Locate Cesium headers from install directory
find_path(CESIUM_NATIVE_INCLUDE_DIR CesiumUtility/Uri.h
    PATHS ${CESIUM_NATIVE_INSTALL_DIR}
    PATH_SUFFIXES include
    NO_DEFAULT_PATH)

set(CESIUM_ANY_LIBRARY_MISSING FALSE)
set(CESIUM_NATIVE_IMPORT_LIBRARIES "")

# Macro to locate Cesium libraries from build directory
macro(find_cesium_library MY_LIBRARY_VAR MY_LIBRARY_NAME)
    unset(${MY_LIBRARY_VAR}_LIBRARY_DEBUG CACHE)
    unset(${MY_LIBRARY_VAR}_LIBRARY_RELWITHDEBINFO CACHE)
    unset(${MY_LIBRARY_VAR}_LIBRARY_RELEASE CACHE)

    find_library(${MY_LIBRARY_VAR}_LIBRARY_DEBUG
        NAMES ${MY_LIBRARY_NAME}d ${MY_LIBRARY_NAME}
        PATHS ${CESIUM_NATIVE_DIR}/${MY_LIBRARY_NAME}/Debug
        NO_DEFAULT_PATH)

    find_library(${MY_LIBRARY_VAR}_LIBRARY_RELEASE
        NAMES ${MY_LIBRARY_NAME}
        PATHS ${CESIUM_NATIVE_DIR}/${MY_LIBRARY_NAME}/Release
        NO_DEFAULT_PATH)

    find_library(${MY_LIBRARY_VAR}_LIBRARY_RELWITHDEBINFO
        NAMES ${MY_LIBRARY_NAME} ${MY_LIBRARY_NAME}
        PATHS ${CESIUM_NATIVE_DIR}/${MY_LIBRARY_NAME}/RelWithDebInfo
        NO_DEFAULT_PATH)

    set(MY_DEBUG_LIBRARY "${${MY_LIBRARY_VAR}_LIBRARY_DEBUG}")
    set(MY_RELEASE_LIBRARY "${${MY_LIBRARY_VAR}_LIBRARY_RELEASE}")
    set(MY_RELWITHDEBINFO_LIBRARY "${${MY_LIBRARY_VAR}_LIBRARY_RELWITHDEBINFO}")

    if(MY_DEBUG_LIBRARY OR MY_RELEASE_LIBRARY OR MY_RELWITHDEBINFO_LIBRARY)
        set(MY_IMPORT_LIBRARY_NAME "CesiumNative::${MY_LIBRARY_NAME}")
        add_library(${MY_IMPORT_LIBRARY_NAME} UNKNOWN IMPORTED)

        if(MY_RELEASE_LIBRARY)
            set_target_properties(${MY_IMPORT_LIBRARY_NAME} PROPERTIES
                IMPORTED_LOCATION "${MY_RELEASE_LIBRARY}")
        endif()

        if(MY_DEBUG_LIBRARY)
            set_target_properties(${MY_IMPORT_LIBRARY_NAME} PROPERTIES
                IMPORTED_LOCATION_DEBUG "${MY_DEBUG_LIBRARY}")
        endif()

        if(MY_RELWITHDEBINFO_LIBRARY)
            set_target_properties(${MY_IMPORT_LIBRARY_NAME} PROPERTIES
                IMPORTED_LOCATION_RELWITHDEBINFO "${MY_RELWITHDEBINFO_LIBRARY}")
        endif()

        list(APPEND CESIUM_NATIVE_IMPORT_LIBRARIES "${MY_IMPORT_LIBRARY_NAME}")
    else()
        message(WARNING "Cesium Native: Required library ${MY_LIBRARY_NAME} not found")
        set(CESIUM_ANY_LIBRARY_MISSING TRUE)
    endif()
endmacro()

# Find core Cesium libraries (from build directory)
find_cesium_library(CESIUM_3DTILES_SELECTION Cesium3DTilesSelection)
find_cesium_library(CESIUM_3DTILES_CONTENT Cesium3DTilesContent)
find_cesium_library(CESIUM_3DTILES_READER Cesium3DTilesReader)
find_cesium_library(CESIUM_3DTILES Cesium3DTiles)
find_cesium_library(CESIUM_GLTF_CONTENT CesiumGltfContent)
find_cesium_library(CESIUM_GLTF_READER CesiumGltfReader)
find_cesium_library(CESIUM_GLTF CesiumGltf)
find_cesium_library(CESIUM_RASTER_OVERLAYS CesiumRasterOverlays)
find_cesium_library(CESIUM_QUANTIZED_MESH_TERRAIN CesiumQuantizedMeshTerrain)
find_cesium_library(CESIUM_GEOSPATIAL CesiumGeospatial)
find_cesium_library(CESIUM_GEOMETRY CesiumGeometry)
find_cesium_library(CESIUM_ASYNC CesiumAsync)
find_cesium_library(CESIUM_JSON_READER CesiumJsonReader)
find_cesium_library(CESIUM_UTILITY CesiumUtility)
find_cesium_library(CESIUM_CURL CesiumCurl)
find_cesium_library(CESIUM_3DTILES_WRITER Cesium3DTilesWriter)
find_cesium_library(CESIUM_GLTF_WRITER CesiumGltfWriter)
find_cesium_library(CESIUM_JSON_WRITER CesiumJsonWriter)
find_cesium_library(CESIUM_ION_CLIENT CesiumIonClient)
find_cesium_library(CESIUM_ITWIN_CLIENT CesiumITwinClient)
find_cesium_library(CESIUM_CLIENT_COMMON CesiumClientCommon)
find_cesium_library(CESIUM_VECTOR_DATA CesiumVectorData)

if(NOT CESIUM_ANY_LIBRARY_MISSING AND CESIUM_NATIVE_INCLUDE_DIR)
    set(CESIUM_NATIVE_FOUND TRUE)

    add_library(CESIUM_NATIVE::CESIUM_NATIVE INTERFACE IMPORTED)

    # Add Windows system libraries
    if(WIN32)
        list(APPEND CESIUM_NATIVE_IMPORT_LIBRARIES
            ws2_32 wldap32 crypt32 bcrypt normaliz iphlpapi secur32
        )
    endif()

    set_property(TARGET CESIUM_NATIVE::CESIUM_NATIVE PROPERTY
        INTERFACE_LINK_LIBRARIES ${CESIUM_NATIVE_IMPORT_LIBRARIES})

    set_property(TARGET CESIUM_NATIVE::CESIUM_NATIVE PROPERTY
        INTERFACE_INCLUDE_DIRECTORIES "${CESIUM_NATIVE_INCLUDE_DIR}")

    message(STATUS "Found Cesium Native: ${CESIUM_NATIVE_DIR}")
    message(STATUS "  Cesium Include: ${CESIUM_NATIVE_INCLUDE_DIR}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CesiumNative DEFAULT_MSG CESIUM_NATIVE_FOUND CESIUM_NATIVE_INCLUDE_DIR)
