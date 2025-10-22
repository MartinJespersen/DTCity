# Problems:
* Sometimes the wrong texture is used on objects, which is a problem with the loading use of textures in the asset manager.

# Things to do:

* Asset Store is currently not thread safe for eviction cases and memory needs to be managed properly.
  * When asset getting evicted we have to make sure no other threads are using the resource.
* Find way to flush logs so the memory does not build up forever
* Threads in asset store should manage have more than one available command buffer in the thread command pool.
* Linux support:
  * entrypoint caller currently implemented for windows should change to linux.
  * HTTP client implementation improvements on linux (move away from httplib)
  * hot reloading refactor to simplify the implementation for linux
  * Base library fixes
* Create method for logging


# On Linux
* For Khronos validation layer support (enabled if DEBUG_BUILD macro is defined) remember to install it:
  * sudo apt install vulkan-validationlayers (Ubuntu)
* Compiling (incl. linking) takes a very long time at the moment.
* hotreloading does not work at the moment, due to the entrypoint library not being unloaded using dlclose
  * This has been a problem defining TRACY_ENABLE in the past as the library overwrites dlclose. This is however not the current problem
* remember to link crypto and ssl libraries when using https and define the macro CPPHTTPLIB_OPENSSL_SUPPORT in httplib

# Mule Test Program
cl /W4 /std:c++20 /wd4201 /wd4505 /wd4005 /wd4838 /wd4244 /wd4996 /wd4310 /wd4245 /wd4100 /EHsc /Z7 mule.cpp /DBUILD_CONSOLE_INTERFACE

# Jpg/png to ktx2
Use the ktx tool from khronos group. Example with mip map generation:
ktx create --format R8G8B8A8_SRGB --generate-mipmap brick_wall.jpg brick_wall.ktx2

# Matsim Simulation
## Prerequisites for interoperability with MATSim (For POC)
* A snapshot writer called called DtCityWriter has been created to allow the Events2Snapshot program (in matsim-libs repository) to output locations
  * Only information about driving vehicles is currently being worked on.
* Files (file names might defer):
  * config.xml: contains configuration settings pointing to the name of the network and events files:
    * network.xml: contains nodes and the links connecting them
    * events.xml: contains events
* OpenStreetMap data is assumed
  * Coordinates used for the nodes in the network file (e.g. `network.xml`) should be in UTM.
  * The CRS (Coordinate Reference System) in epsg:25832 (UTM Zone 32N). This is specified in the configuration file (berlin-v6.4.config.xml).
* File format should be XML for all files
<!--* Only departure and arrival events are supported.
* Only cars are supported (ie. legMode="car")-->

## First integration steps
* Write a simple snapshot writer that just outputs positions and car ids so that it can be used with MATSim.
* Snapshots will not be generated for every frame, but only at specific time intervals but the future positions should be used as milestones for agents in the simulation so they interpolate their current position based on previous and next position.

## Scenario Steps

## Good to know
* matsim-libs uses the class Events2Snapshot for calculating position from events.
* Other related files/classes: Snapshotgenerator
The links in the network file specifies a from and to node, where each node have a 2D coordinate.
The coordinates for a car at a specific point in time are found using the event departure and arrival times.
<event time="1470.0" type="departure" person="349354601" link="44743" legMode="car"  />
<event time="2167.0" type="arrival" person="349354601" link="48406" legMode="car"  />
