// meshlets.words contains the shared bounding sphere and per-LOD error table.
uint model_lod_select(mat4 model);
uint model_lod_select(mat4 model)
{
    vec3 center = uintBitsToFloat(uvec3(meshlets.words[MODEL_GEOMETRY_CENTER_X_OFFSET],
        meshlets.words[MODEL_GEOMETRY_CENTER_Y_OFFSET], meshlets.words[MODEL_GEOMETRY_CENTER_Z_OFFSET]));
    float radius = uintBitsToFloat(meshlets.words[MODEL_GEOMETRY_RADIUS_OFFSET]);
    // Frobenius norm bounds stretch even for nonuniform scale/shear.
    float stretch = sqrt(dot(model[0].xyz, model[0].xyz) + dot(model[1].xyz, model[1].xyz) + dot(model[2].xyz, model[2].xyz));
    vec3 view_center = (camera_ubo.view * model * vec4(center, 1)).xyz;
    float nearest_depth = -view_center.z - radius * stretch;
    float projection_scale = abs(camera_ubo.proj[1][1]) * camera_ubo.viewport_dim.y * 0.5;
    uint lod = 0;
    if (pc.lod_error_pixels > 0 && nearest_depth > 0)
    {
        for (uint candidate = 1; candidate < meshlets.words[MODEL_GEOMETRY_LOD_COUNT_OFFSET]; ++candidate)
        {
            uint lod_entry = MODEL_GEOMETRY_LOD_TABLE_OFFSET + candidate * MODEL_LOD_ENTRY_WORD_COUNT;
            float error = uintBitsToFloat(meshlets.words[lod_entry + MODEL_LOD_ERROR_OFFSET]);
            float pixels = error * stretch * projection_scale / nearest_depth;
            if (pixels <= pc.lod_error_pixels) { lod = candidate; }
        }
    }
    return lod;
}
