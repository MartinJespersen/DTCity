# About

DTCity is an application for visualizing city data in 3D. 
The visualization is currently able to receive bounding box coordinates in WGS84 as input through the command line. The data is from exclusively from OpenStreetMap at the moment, where information about roads and buildings is used.
A basic simulation of cars are also provided that show support for visualizing meshes from .gltf file formats as well.

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



