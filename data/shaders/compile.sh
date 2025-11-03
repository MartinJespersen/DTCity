#!/bin/zsh
set -e


pushd model_3d
glslc model_3d.vert -o model_3d_vert.spv
glslc model_3d.frag -o model_3d_frag.spv
popd

pushd model_3d_instancing
glslc model_3d_instancing.vert -o model_3d_instancing_vert.spv
glslc model_3d_instancing.frag -o model_3d_instancing_frag.spv
popd
