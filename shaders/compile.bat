@echo off

:: Exit on error
setlocal

pushd model_3d_instancing
call glslc model_3d_instancing.frag -o model_3d_instancing_frag.spv
call glslc model_3d_instancing.vert -o model_3d_instancing_vert.spv
popd

pushd model_3d
call glslc model_3d.frag -o model_3d_frag.spv
call glslc model_3d.vert -o model_3d_vert.spv
popd
endlocal
