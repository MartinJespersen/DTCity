namespace city
{
static void
CityInit(wrapper::VulkanContext* vk_ctx, City* city, String8 cwd)
{
    city->arena = ArenaAlloc();
    city->road.node_slot_count = 100;

    city->w_road = PushStruct(city->arena, wrapper::Road);
    city->road.road_height = 10.0f;
    city->road.road_width = 3.0f;

    wrapper::RoadInit(vk_ctx, city, cwd);
}

static void
CityUpdate(City* city, wrapper::VulkanContext* vk_ctx, U32 image_index)
{
    // ~mgj: road building update
    wrapper::Road* w_road = city->w_road;
    Road* road = &city->road;
    wrapper::RoadUpdate(road, w_road, vk_ctx, image_index);
};

static void
CityCleanup(City* city, wrapper::VulkanContext* vk_ctx)
{
    wrapper::RoadCleanup(city, vk_ctx);
}

static inline RoadNode*
NodeFind(Road* road, U64 node_id)
{
    RoadNodeSlot node_slot = road->nodes[node_id % road->node_slot_count];

    RoadNode* node = node_slot.first;
    for (; node <= node_slot.last; node = node->next)
    {
        if (node->id == node_id)
        {
            break;
        }
    }
    return node;
}

static RoadQuadCoord
RoadQuadUtmFromQgisRoadSegment(F64 lat_bot, F64 lon_bot, F64 lat_top, F64 lon_top,
                               F64 center_transform_x, F64 center_transform_y, F32 road_half_width)
{
    F64 road_node_0_x;
    F64 road_node_0_y;
    F64 road_node_1_x;
    F64 road_node_1_y;

    char UTMZone[10];
    UTM::LLtoUTM(lat_bot, lon_bot, road_node_0_y, road_node_0_x, UTMZone);
    UTM::LLtoUTM(lat_top, lon_top, road_node_1_y, road_node_1_x, UTMZone);

    glm::vec2 road_0_pos =
        glm::vec2(road_node_0_x + center_transform_x, road_node_0_y + center_transform_y);
    glm::vec2 road_1_pos =
        glm::vec2(road_node_1_x + center_transform_x, road_node_1_y + center_transform_y);

    glm::vec2 road_dir = road_0_pos - road_1_pos;
    glm::vec2 orthogonal_vec = {road_dir.y, -road_dir.x};
    glm::vec2 normal = glm::normalize(orthogonal_vec);
    glm::vec2 normal_scaled = normal * road_half_width;

    RoadQuadCoord road_quad_coord;
    road_quad_coord.pos[0] = road_0_pos + normal_scaled;
    road_quad_coord.pos[1] = road_0_pos + (-normal_scaled);
    road_quad_coord.pos[2] = road_1_pos + normal_scaled;
    road_quad_coord.pos[3] = road_1_pos + (-normal_scaled);
    return road_quad_coord;
}

static RoadQuadCoord
RoadSegmentFromNodeIds(Road* road, RoadWay* way, U32 index_0, U32 index_1, F64 center_transform_x,
                       F64 center_transform_y, F32 road_half_width)
{
    Assert(index_0 < way->node_count);
    Assert(index_1 < way->node_count);

    U64 roadseg0_node_id_0 = way->node_ids[index_0];
    U64 roadseg0_node_id_1 = way->node_ids[index_1];
    RoadNode* road_node_0 = NodeFind(road, roadseg0_node_id_0);
    RoadNode* road_node_1 = NodeFind(road, roadseg0_node_id_1);
    RoadQuadCoord road_quad_coords = RoadQuadUtmFromQgisRoadSegment(
        road_node_0->lat, road_node_0->lon, road_node_1->lat, road_node_1->lon, center_transform_x,
        center_transform_y, road_half_width);
    return road_quad_coords;
}

static void
RoadsBuild(Arena* arena, City* city)
{
    Road* road = &city->road;
    ScratchScope scratch = ScratchScope(&arena, 1);

    // TODO: make this an input and check for input conditions
    F64 lat_low = 56.16923976826141;
    F64 lon_low = 10.1852768812041;
    F64 lat_high = 56.17371342689877;
    F64 lon_high = 10.198376789774187;

    F64 out_northing_low = 0;
    F64 out_easting_low = 0;
    char zone_low[265];
    UTM::LLtoUTM(lat_low, lon_low, out_easting_low, out_northing_low, zone_low);
    F64 out_northing_high = 0;
    F64 out_easting_high = 0;
    char zone_high[265];
    UTM::LLtoUTM(lat_high, lon_high, out_easting_high, out_northing_high, zone_high);

    F64 meter_diff_x = out_easting_high - out_easting_low;
    F64 meter_diff_y = out_northing_high - out_northing_low;
    F64 lon_diff = lon_high - lon_low;
    F64 lat_diff = lat_high - lat_low;
    F64 x_scale = meter_diff_x / lon_diff;
    F64 y_scale = meter_diff_y / lat_diff;

    HTTP_RequestParams params = {};
    params.method = HTTP_Method_Post;
    params.content_type = S("text/html");

    const char* query = R"(data=
        [out:json] [timeout:25];
        (
          way["highway"](%f, %f, %f, %f);
        );
        out body;
        >;
        out skel qt;
    )";
    String8 query_str =
        PushStr8F(scratch.arena, (char*)query, lat_low, lon_low, lat_high, lon_high);

    String8 host = S("https://overpass-api.de");
    String8 path = S("/api/interpreter");
    HTTP_Response response = HTTP_Request(scratch.arena, host, path, query_str, &params);

    String8 content = Str8((U8*)response.body.str, response.body.size);

    wrapper::OverpassHighways(scratch.arena, road, content);

    F32 road_half_width = road->road_width / 2.0f; // Example value, adjust as needed
    F64 long_low_utm;
    F64 lat_low_utm;
    F64 long_high_utm;
    F64 lat_high_utm;
    char utm_zone[10];
    UTM::LLtoUTM(lat_low, lon_low, lat_low_utm, long_low_utm, utm_zone);
    UTM::LLtoUTM(lat_high, lon_high, lat_high_utm, long_high_utm, utm_zone);
    F64 center_transform_x = -(long_low_utm + long_high_utm) / 2.0;
    F64 center_transform_y = -(lat_low_utm + lat_high_utm) / 2.0;

    // road->way_count = 2;
    U64 node_count = 0;
    for (U32 way_index = 0; way_index < road->way_count; way_index++)
    {
        RoadWay* way = &road->ways[way_index];
        // + 1 is the result of adding two number:
        // -1 get the number of road segments (because the first and last vertices are shared
        // between segments)
        node_count += way->node_count - 1;
    }
    // the addition of road->way_count * 2 is due to the triangle strip topology used to render the
    // road segments
    U64 total_vert_count = node_count * 7 + road->way_count * 2;

    road->vertex_buffer =
        BufferAlloc<RoadVertex>(city->arena, total_vert_count); // 4 vertices per line segment

    U32 road_segment_count = 0;
    U32 current_vertex_index = 0;
    for (U32 way_index = 0; way_index < road->way_count; way_index++)
    {
        RoadWay* way = &road->ways[way_index];

        // TODO: what if way has less than two nodes?
        // first road segment

        RoadQuadCoord road_quad_coords_first = RoadSegmentFromNodeIds(
            road, way, 0, 1, center_transform_x, center_transform_y, road_half_width);
        // last road segment
        U32 second_to_last_node_index = way->node_count - 2;
        U32 last_node_index = way->node_count - 1;
        RoadQuadCoord road_quad_coords_last =
            RoadSegmentFromNodeIds(road, way, second_to_last_node_index, last_node_index,
                                   center_transform_x, center_transform_y, road_half_width);

        // U64 current_vertex_index = road_segment_count * 4 + way_index * 2;
        road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords_first.pos[0];
        // for each road segment (road segment = two connected nodes)

        if (way->node_count < 2)
        {
            exitWithError("expected at least one road segment comprising of two nodes");
        }

        RoadQuadCoord road_quad_coords;
        RoadQuadCoord road_quad_coords_next;
        for (U32 node_index = 1; node_index < way->node_count; node_index++)
        {
            if (node_index == 1)
            {
                road_quad_coords =
                    RoadSegmentFromNodeIds(road, way, node_index - 1, node_index,
                                           center_transform_x, center_transform_y, road_half_width);
            }

            // +1 due to duplicate of first and last way node to seperate roadways in triangle strip
            // topology
            road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords.pos[0];
            road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords.pos[1];
            road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords.pos[2];
            road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords.pos[3];

            if (node_index < way->node_count - 1)
            {
                road_quad_coords_next =
                    RoadSegmentFromNodeIds(road, way, node_index, node_index + 1,
                                           center_transform_x, center_transform_y, road_half_width);

                road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords_next.pos[1];
                road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords.pos[2];
                road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords.pos[3];

                road_quad_coords = road_quad_coords_next;
            }
        }
        road->vertex_buffer.data[current_vertex_index++].pos = road_quad_coords_last.pos[3];
        road_segment_count += way->node_count - 1;
    }
}

struct VK_Texture
{
    VkImage image;
    VkDeviceMemory image_memory;
    VkImageView image_view;
    VkSampler sampler;

    VkFormat format;
    U32 mip_level_count;
};

struct VK_TextureResult
{
    VK_Texture texture;
};

// TODO: check for blitting format beforehand
// void
// RoadTextureCreate(Arena* arena, Context* ctx, String8 texture_name, VkFormat blit_format)
// {
//     ScratchScope scratch = ScratchScope(0, 0);

//     wrapper::VulkanContext* vk_ctx = ctx->vk_ctx;
//     VK_TextureResult result = {0};
//     VK_Texture* vk_texture = PushStruct(arena, VK_Texture);
//     vk_texture->format = blit_format;

//     // check for blitting format
//     VkFormatProperties formatProperties;
//     vkGetPhysicalDeviceFormatProperties(vk_ctx->physical_device, blit_format, &formatProperties);
//     if (!(formatProperties.optimalTilingFeatures &
//           VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
//     {
//         exitWithError("texture image format does not support linear blitting!");
//     }

//     S32 tex_width, tex_height, tex_channels;
//     stbi_uc* pixels = stbi_load((char*)ctx->texture_path.str, &tex_width, &tex_height,
//                                 &tex_channels, STBI_rgb_alpha);
//     VkDeviceSize image_size = tex_width * tex_height * 4;
//     U32 mip_level = (U32)(floor(log2(Max(tex_width, tex_height)))) + 1;
//     vk_texture->mip_level_count = mip_level;

//     if (!pixels)
//     {
//         exitWithError("failed to load texture image!");
//     }

//     wrapper::internal::BufferAllocation texture_staging_buffer = wrapper::BufferAllocationCreate(
//         vk_ctx->allocator, image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
//         VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

//     void* data;
//     vmaMapMemory(vk_ctx->allocator, texture_staging_buffer.allocation, &data);
//     // vkMapMemory(vk_ctx->device, texture_staging_buffer, 0, image_size, 0, &data);
//     memcpy(data, pixels, image_size);
//     vmaUnmapMemory(vk_ctx->allocator, texture_staging_buffer.allocation);
//     // vkUnmapMemory(vk_ctx->device, texture_staging_buffer);

//     stbi_image_free(pixels);

//     wrapper::internal::ImageAllocation image_alloc =
//         wrapper::VK_ImageCreate(vk_ctx->allocator, tex_width, tex_height, VK_SAMPLE_COUNT_1_BIT,
//                                 VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
//                                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
//                                 |
//                                     VK_IMAGE_USAGE_SAMPLED_BIT,
//                                 mip_level, VMA_MEMORY_USAGE_AUTO);

//     VkCommandBuffer command_buffer = VK_BeginSingleTimeCommands(vk_ctx);
//     wrapper::VK_ImageLayoutTransition(command_buffer, terrain->vk_texture_image,
//                                       VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED,
//                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
//                                       terrain->vk_mip_levels);
//     wrapper::VK_ImageFromBufferCopy(command_buffer, vk_texture_staging_buffer,
//                                     terrain->vk_texture_image, tex_width, tex_height);
//     wrapper::VK_GenerateMipmaps(
//         command_buffer, terrain->vk_texture_image, tex_width, tex_height,
//         terrain->vk_mip_levels); // TODO: mip maps are usually not generated at runtime. They are
//                                  // usually stored in the texture file
//     VK_EndSingleTimeCommands(vk_ctx, command_buffer);

//     vkDestroyBuffer(vk_ctx->device, vk_texture_staging_buffer, nullptr);
//     vkFreeMemory(vk_ctx->device, vk_texture_staging_buffer_memory, nullptr);

//     wrapper::ImageViewResourceCreate(&terrain->vk_texture_image_view, vk_ctx->device,
//                                 terrain->vk_texture_image, VK_FORMAT_R8G8B8A8_SRGB,
//                                 VK_IMAGE_ASPECT_COLOR_BIT, terrain->vk_mip_levels);
//     wrapper::VK_SamplerCreate(&terrain->vk_texture_sampler, vk_ctx->device, VK_FILTER_LINEAR,
//                               VK_SAMPLER_MIPMAP_MODE_LINEAR, terrain->vk_mip_levels,
//                               vk_ctx->physical_device_properties.limits.maxSamplerAnisotropy);
// }
} // namespace city
