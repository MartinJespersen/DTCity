set -e

pushd terrain
glslc terrain.frag -o terrain_frag.spv
glslc terrain.vert -o terrain_vert.spv
glslc terrain.tesc -o terrain_tesc.spv
glslc terrain.tese -o terrain_tese.spv
popd

pushd road
glslc road.frag -o road_frag.spv
glslc road.vert -o road_vert.spv
popd
