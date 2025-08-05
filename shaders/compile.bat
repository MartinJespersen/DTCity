@echo off

:: Exit on error
setlocal

pushd car
call glslc car.frag -o car_frag.spv
call glslc car.vert -o car_vert.spv
popd

pushd road
call glslc road.frag -o road_frag.spv
call glslc road.vert -o road_vert.spv
popd

endlocal
