@echo off

:: Exit on error
setlocal

:: Compile shaders
call glslc terrain.frag -o terrain_frag.spv
call glslc terrain.vert -o terrain_vert.spv
call glslc terrain.tesc -o terrain_tesc.spv
call glslc terrain.tese -o terrain_tese.spv

endlocal