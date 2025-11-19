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

## Controls

Movement: WASD keys (first-person style)
* W – forward
* S – backward
* A – strafe left
* D – strafe right

Look around: Hold the left mouse button and move the mouse
Road information: Hover the mouse cursor over a road to display available details (name, type, etc.), provided the data exists in OpenStreetMap.
