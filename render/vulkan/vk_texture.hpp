struct VK_Tex2D
{
    VK_Tex2D* next;
    wrapper::BufferAllocation staging_buffer;
    wrapper::ImageResource image_resource;
    R_Tex2DFormat format;
    VkSampler sampler;
    VkDescriptorSet desc_set;
};

struct VK_TextureList
{
    VK_Tex2D* first;
    VK_Tex2D* last;
};

////////////////////////////////////
// ~mgj: Globals
U8 r_tex2d_format_bytes_per_pixel_table[9] = {
    1, 2, 4, 4, 2, 8, 4, 8, 16,
};
VK_TextureList r_tex2d_list;
VK_Tex2D* r_tex2d_free_list;
////////////////////////////////////

static VK_Tex2D*
VK_TextureFromHandle(R_Handle handle);
