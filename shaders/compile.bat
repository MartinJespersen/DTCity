@echo off

:: Exit on error
setlocal

:: Compile shaders
pushd terrain
call glslc terrain.frag -o terrain_frag.spv
call glslc terrain.vert -o terrain_vert.spv
call glslc terrain.tesc -o terrain_tesc.spv
call glslc terrain.tese -o terrain_tese.spv
popd

pushd road
call glslc road.frag -o road_frag.spv
call glslc road.vert -o road_vert.spv
call glslc road.geom -o road_geom.spv
popd

endlocal
