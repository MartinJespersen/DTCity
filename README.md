# About

DTCity is an application for visualizing city data in 3D. 
The visualization is able to receive bounding box coordinates as input through the command line. Information about roads and buildings are fetched from OpenStreetMap. The information is either visualized in 3D or shown as text in a pop up window, when the object is hovered over.
A basic simulation of cars are also provided that show support for visualizing mesh data provided through a .gltf or .glb file.

# Usage
Binary executables for Linux and Windows along with dependencies are attached to each version of the application released on Github. 
The [usage-guide.pdf](docs/usage-guide.pdf) explains how to use the application after launch. 

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
To test the application for linux with WSL2 with docker, run the following commands inside WSL2. Make sure to enable WSL integration from the settings inside Docker Desktop with the distro you run the commands from.
```bash
docker compose up -d --build
docker compose exec city
apt update && apt install -y libvulkan1
./city
```

## 1. Create build directory

The CMakePreset.json file in the project root consists of 3 different build configurations for both x64 windows and x64 linux. 

The combinations are:
* debug-windows
* debug-linux
* release-windows
* release-linux
* profile-windows
* profile-linux

An example of setting up the build for the debug preset for windows is shown below:
- cmake --preset=debug-windows

For each of the 3 different build configurations, a build directory is created (build/<configuration_name>).
The different build configuration are: debug, release and profile.

To build for debug on either windows or linux, run the following commands:
- cmake --build build/debug

### C Macros
The following application specific macros are used to enable address sanitization, build tools and profiling:
* -DBUILD_DEBUG (Additional debug information e.g vulkan validation layer support)
* -DASAN_ENABLE=ON (enable address sanitizer support)
* -DTRACY_PROFILE_ENABLE (Enable tracy profiling)

CMake presets define these macros based on what type of build configuration is used - debug, release or profile. These defaults can be changed e.g. you might want to enable address sanitization in a profile build.
