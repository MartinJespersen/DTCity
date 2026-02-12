# Building cesium-native for DTCity

DTCity optionally supports cesium-native for 3D Tiles rendering. cesium-native must be built separately using its own ezvcpkg build system.

## Prerequisites

- CMake 3.18+
- Visual Studio 2022 (or compatible C++17 compiler)
- Git

## Building cesium-native

1. Clone cesium-native:
```bash
git clone https://github.com/CesiumGS/cesium-native.git
cd cesium-native
git checkout v0.43.0  # Use a stable version
```

2. Create build directory and configure:
```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=C:/cesium-native-install
```

The build will automatically use ezvcpkg to fetch and build all dependencies with compatible versions.

3. Build and install:
```bash
cmake --build . --config Release
cmake --install . --config Release
```

For debug builds:
```bash
cmake --build . --config Debug
cmake --install . --config Debug
```

## Configuring DTCity with cesium-native

When configuring DTCity with CMake, set the `CESIUM_NATIVE_DIR` variable to point to your cesium-native installation:

```bash
cmake .. -DCESIUM_NATIVE_DIR=C:/cesium-native-install
```

Or set the environment variable:
```bash
set CESIUM_NATIVE_DIR=C:/cesium-native-install
cmake ..
```

If cesium-native is found, you'll see:
```
-- Found CesiumNative
```

If not found, the build will proceed without 3D Tiles support:
```
-- Cesium Native not found - building without 3D Tiles support
```

## Verifying the installation

The FindCesiumNative.cmake module looks for these components in `CESIUM_NATIVE_DIR`:
- `include/CesiumUtility/Uri.h` (header files)
- `lib/Cesium3DTilesSelection.lib` (and other libraries)

Make sure your installation contains both the `include/` and `lib/` directories with all cesium-native libraries.

## Troubleshooting

### GLM compatibility issues
cesium-native bundles its own GLM and other dependencies. The FindCesiumNative.cmake ensures cesium-native headers are included BEFORE other project headers to avoid conflicts.

### Missing libraries
If you get missing library errors, ensure you built both Release and Debug configurations, or match your DTCity build type to your cesium-native build type.

### Version compatibility
Different cesium-native versions may have different API signatures. The integration code in `src/cesium/` is written for cesium-native v0.43.0 - v0.50.0.
