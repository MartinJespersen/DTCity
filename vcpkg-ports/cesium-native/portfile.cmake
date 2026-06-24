vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
  FEATURES
  dependencies-only CESIUM_NATIVE_DEPS_ONLY
)

if(CESIUM_NATIVE_DEPS_ONLY)
    message(STATUS "skipping installation of cesium-native")
    set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
    return()
endif()

vcpkg_from_github(
  OUT_SOURCE_PATH SOURCE_PATH
  REPO CesiumGS/cesium-native
  REF "v${VERSION}"
  SHA512 a329c057e90b1b5565f34c29d0ce6dd42af0d33d30f14a4367cadfa12af5b69b184fe6555293283e2a1c1938612b837b73dbac1f5cc481c05e63037990b75111
  HEAD_REF main
  PATCHES
    config.patch
)

vcpkg_cmake_configure(
  SOURCE_PATH "${SOURCE_PATH}"
  OPTIONS
    -DCESIUM_USE_EZVCPKG=OFF
    -DCESIUM_TESTS_ENABLED=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH share/cesium-native/cmake PACKAGE_NAME cesium-native)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
