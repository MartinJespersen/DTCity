# How To Use

The program is currently able to show a patch of land in 3D through WGS84 coordinates specified on the command line. The bounding box consists of two 2D coordinates, the first coordinate being the lower left corner (p0) and the second coordinate being the upper right corner (p1). Each point has a longitude (lon) and latitude (lat) so the coordinates for the p0 would be lon_p0 and lat_p0. The commandline arguments are given as follows:  
```
./city lon_p0 lat_p0 lon_p1 lat_p1
```
example with floating point value:
```
./city 13.388860 52.517037 13.428055 52.539674
```

When the application has started it is possible to navigate like you would do in a first person shooter. You can control the movement of the camera using the WASD keys and the view direction by the moving the mouse over the application window while holding down the left mouse button. 
It is also possible to retrieve information about the roads by hovering over them. Of course this requires the information to be available in OpenStreetMap which is not always the case.
