# About

DTCity is an application for viewing city data in 3D visualization. 
The visualization is currently able to receive a bounding box as commandline and from this data the visualization is able to show roads and buildings.
A basic simulation of cars are also provided that show support for visualizing meshes from .gltf file formats as well.
The application uses OpenStreetMap as the data source for road and building locations, but upcoming versions of the software will add support for other data sources.  

# How To Use

The program renders a selected area of the Earth's surface in 3D using WGS84 coordinates (longitude/latitude) provided as command-line arguments.
The area is defined by a rectangular bounding box specified by two corners:

* p0 – lower-left corner (south-west)
* p1 – upper-right corner (north-east)

Each corner is given as longitude followed by latitude (in decimal degrees).
```
./city lon_p0 lat_p0 lon_p1 lat_p1
```
example with floating point value:
```
./city 13.388860 52.517037 13.428055 52.539674
```

Caution!
Keep the bounding box to an area that is not over 1 square kilometer to avoid long loading times.
The application does not currently implement any form of culling. 

## Controls

Movement: WASD keys (first-person style)
* W – forward
* S – backward
* A – strafe left
* D – strafe right

Look around: Hold the left mouse button and move the mouse 

Road information: Hover the mouse cursor over a road to display available details (name, type, etc.), provided the data exists in OpenStreetMap.

# Build instructions

## Windows Prerequisites
* Install the Microsoft C/C++ Build Tools to install the MSVC compiler, CMake and vcpkg.
* Setup environment by running the script: ```vcvarsall.bat x64``` or ```use the x64 Native Tools Command Prompt``` that will automatically run the script when started.

## Linux Prerequisites (Ubuntu/Debian based)
A docker file is provided showing how to setup a ubuntu based build environment. 
The required packages can be installed with the following command: 
```bash
apt-get update && apt-get install -y \
    python3 \
    build-essential \
    git \
    curl \
    zip \
	unzip \
    tar \
    cmake \
    libvulkan1 \
    libgl1-mesa-dev xorg-dev libwayland-dev libxkbcommon-dev wayland-protocols extra-cmake-modules
```
To test the application for linux with WSL2 run the following commands inside WSL2. The commands should be run inside WSL so make sure to enable WSL integration for Docker Desktop with the distro you run the commands from.
```bash
docker compose up -d --build
docker compose exec city
apt update && apt install -y libvulkan1
./city
```

## 1. Create build directory

The CMakePreset.json file in the project root consists of 3 different build configurations for both windows and linux for the x64 architecture. 

The combinations are:
* debug-windows
* debug-linux
* release-windows
* release-linux
* profile-windows
* profile-linux

An example of setting up the build for the debug preset for windows is shown below:
- cmake --preset=debug-windows

To build simply run:
- cmake --build build

### C Macros
The following application specific macros are used to enable address sanitization, build tools and profiling:
* -DBUILD_DEBUG (Additional debug information e.g vulkan validation layer support)
* -DASAN_ENABLE=ON (enable address sanitizer support)
* -DTRACY_PROFILE_ENABLE (Enable tracy profiling)

CMake presets define these macros based on what type of build configuration is used - debug, release or profile. These defaults can be changed e.g. you might want to enable address sanitization in a profile build.

# Helpers
## Jpg/png to ktx2
Use the ktx tool from khronos group. Example with mip map generation:
ktx create --format R8G8B8A8_SRGB --generate-mipmap brick_wall.jpg brick_wall.ktx2
