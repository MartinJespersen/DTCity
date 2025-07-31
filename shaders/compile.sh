set -e

pushd road
glslc road.frag -o road_frag.spv
glslc road.vert -o road_vert.spv
popd
