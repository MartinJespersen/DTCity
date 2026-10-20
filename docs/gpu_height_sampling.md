# GPU agent height sampling

Tile workers build triangle BVHs and upload the final mesh buffers. Completed
loads, replacements and unloads update the city's registry. `TileBvh::update()`
starts an outer BVH rebuild on a worker and publishes it only after upload
completion. Sampling retains the published version until the GPU finishes.

The height compute shader traverses the outer BVH and each candidate mesh's
triangle BVH, then returns the closest nonnegative ray intersection. It uses a
retained table of buffer device addresses, so mesh count does not require a large
storage-buffer descriptor array. Shared structures in `shader_shared.h` define
the C++/GLSL layout. Mesh ray directions are transformed without normalization
to preserve distances across nonuniform transforms.

## Surface and coordinates

Queries use uploaded geometry from the current tile draw list, including tiles
that are fading. Loaded meshes stay registered with stable handles, but
`TileMeshBvhEntry::query_enabled` controls their membership in the outer BVH.
Each frame, draw-list changes enable or disable those entries; unchanged
selection and hidden mesh replacements do not rebuild the outer BVH.
Queries do not download missing tiles or force the most detailed Cesium LOD.
Overlapping drawn entries are all tested; the closest intersection wins.
A miss preserves the agent's previous height. Agents outside the drawn tiles
keep their previous height until geometry there is drawn again.

Agent rays start above the published city-local bounds and follow the negative
WGS84 ellipsoid normal at the agent's longitude/latitude. The ray's reference
surface is the ellipsoid at height zero. `origin_height - hit_distance` gives
ellipsoid height in metres, which matches the existing agent height field.

Only one query per agent is pending at a time. Results must match the request
ID, agent slot generation and streaming epoch. Samples are discarded if the
agent moved more than five metres before collection. These cases preserve the
previous height and allow another sample.
Completed hits are also checked against the current draw list using both the
entry index and generation. This rejects hidden parent hits from older retained
BVH versions while the replacement hierarchy is being built or uploaded.

Each hit returns the mesh's stable registry handle (index and generation) in
`TileHeightResult::entry_handle`. The shader copies the handle from the winning
entry, so it remains meaningful after BVH sorting, mesh removal or slot reuse.
A miss returns a zero handle. Temporary per-agent file logging used to diagnose
the loaded-parent issue has been removed.

Agent transforms are recalculated after movement or a changed height. Repeated
samples of an unchanged height do not repeat ECEF conversion and bounds updates.

## Completion and shutdown

Each sampler owns two reusable batches. Each batch has query storage, device
result storage, mapped readback memory, a command pool and a fence. Collection
polls the fence and invalidates mapped readback memory before copying results.
Partial collection keeps the batch occupied. Saturated samplers return false
without waiting. Completed GPU work releases its BVH reference even if results
have not yet been collected.

Destroying an in-flight sampler marks its batches for retirement.
`gpu_work_update()` releases them after completion. No city, agent, or sampler
pointer is used by deferred retirement. Device-idle waits remain confined to
renderer shutdown and test executables.

## Verification

With `BUILD_TESTS=ON`, build `city_tests` and `tile_height_compute_test`. The
`tile_height_compute` CTest test executes the production shader and dispatch
helper without a window. It compares GPU hits against double-precision CPU
triangle intersections, including overlaps, nonuniform transforms, double-sided
faces, parallel rays, offsets, partial workgroups, empty geometry and an
in-flight version replacement. Synchronization validation is enabled when the
Khronos validation layer is installed. Devices without the required Vulkan 1.3
compute features return CTest's skip code 77.

For the shared Linux preset:

```sh
cmake --preset debug-no-asan
cmake --build build/Linux/debug-no-asan --target city_tests tile_height_compute_test -j4
ctest --test-dir build/Linux/debug-no-asan -R 'city_tests|tile_height_compute' --output-on-failure
```

The headless executable accepts the shader path and an optional device index.
It reports GPU dispatch/copy timestamps and submission/readback wall time.
The large-mesh CPU test reports a synthetic 10,000-mesh BVH rebuild time.
These are test workloads, not an application-wide speedup measurement. Runtime
BVH updates, builds, submissions and collections also have profiler scopes.

With `TRACY_PROFILE_ENABLE=ON`, GPU zones cover all graphics and compute
pipeline runs, including ImGui. `tile_height_sampling` covers the height
dispatch and its readback copy; `model_lod_compute` covers agent LOD dispatches.
These use the existing frame Tracy contexts and nonblocking timestamp collection.
