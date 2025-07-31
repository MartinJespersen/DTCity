@echo off

:: Exit on error
setlocal

pushd road
call glslc road.frag -o road_frag.spv
call glslc road.vert -o road_vert.spv
popd

endlocal
