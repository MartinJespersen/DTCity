@echo off

:: Exit on error
setlocal

:: Compile shaders
call glslc terrain.frag -o terrain_frag.spv
call glslc terrain.vert -o terrain_vert.spv

endlocal