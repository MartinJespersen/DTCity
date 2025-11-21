

#pragma once

////////////////////////////////
//~ mgj: Handle Type
union R_Handle
{
    U64 u64[1];
    U32 u32[2];
    U16 u16[4];
};

/////////////////////////////////
enum R_ResourceKind
{
    R_ResourceKind_Static,
    R_ResourceKind_Dynamic,
    R_ResourceKind_Stream,
    R_ResourceKind_COUNT,
};

enum R_Tex2DFormat
{
    R_Tex2DFormat_R8,
    R_Tex2DFormat_RG8,
    R_Tex2DFormat_RGBA8,
    R_Tex2DFormat_BGRA8,
    R_Tex2DFormat_R16,
    R_Tex2DFormat_RGBA16,
    R_Tex2DFormat_R32,
    R_Tex2DFormat_RG32,
    R_Tex2DFormat_RGBA32,
    R_Tex2DFormat_COUNT,
};

// ~mgj: Sampler
enum R_MipMapMode
{
    R_MipMapMode_Nearest = 0,
    R_MipMapMode_Linear = 1,
};

enum R_Filter
{
    R_Filter_Nearest = 0,
    R_Filter_Linear = 1,
};

enum R_SamplerAddressMode
{
    R_SamplerAddressMode_Repeat,
    R_SamplerAddressMode_MirroredRepeat,
    R_SamplerAddressMode_ClampToEdge,
    R_SamplerAddressMode_ClampToBorder,
};

struct r_TextureInfo
{
    U32 base_width;
    U32 base_height;
    U32 base_depth;
    U64 base_size;
    Buffer<U32> mip_level_offsets;
    Buffer<U8> data;
    U8* image_start_ptr;
};

struct R_SamplerInfo
{
    R_Filter min_filter;
    R_Filter mag_filter;
    R_MipMapMode mip_map_mode;
    R_SamplerAddressMode address_mode_u;
    R_SamplerAddressMode address_mode_v;
};

enum R_BufferType
{
    R_BufferType_Invalid,
    R_BufferType_Vertex,
    R_BufferType_Index
};

struct R_BufferInfo
{
    Buffer<U8> buffer;
    U64 type_size;
    R_BufferType buffer_type;
};

enum R_AssetItemType
{
    R_AssetItemType_Undefined,
    R_AssetItemType_Texture,
    R_AssetItemType_Buffer
};

enum R_PipelineUsageType
{
    R_PipelineUsageType_Invalid,
    R_PipelineUsageType_3D,
    R_PipelineUsageType_3DInstanced,
};

template <typename T> struct R_AssetItem
{
    R_AssetItem* next;
    B32 is_loaded;
    T item;
};

template <typename T> struct R_AssetItemList
{
    R_AssetItem<T>* first;
    R_AssetItem<T>* last;
};

struct R_TextureLoadingInfo
{
    r_TextureInfo texture_info;
};

struct R_AssetLoadingInfo
{
    R_Handle handle;
    R_AssetItemType type;
    union
    {
        R_TextureLoadingInfo texture_info;
        R_BufferInfo buffer_info;
    } extra_info;
};

struct R_ThreadInput
{
    Arena* arena;
    R_AssetLoadingInfo asset_info;
};
static R_Handle
R_HandleZero();

template <typename T>
static R_BufferInfo
R_BufferInfoFromTemplateBuffer(Buffer<T> buffer, R_BufferType buffer_type);

//////////////////////////////////////////////////////////////////////////
// ~mgj: function declaration to be implemented by backend

static void
R_RenderCtxCreate(String8 shader_path, io_IO* io_ctx, async::Threads* thread_pool);
static void
R_RenderCtxDestroy();
static void
R_RenderFrame(Vec2U32 framebuffer_dim, B32* in_out_framebuffer_resized, ui_Camera* camera,
              Vec2S64 mouse_cursor_pos);
static void
R_GpuWorkDoneWait();
static void
R_NewFrame();
static U64
r_latest_hovered_object_id_get();

// ~mgj: Texture loading interface
g_internal R_Handle
r_texture_handle_create(R_SamplerInfo* sampler_info, R_PipelineUsageType pipeline_usage_type,
                        r_TextureInfo* tex_create_info);
g_internal R_Handle
r_texture_load_async(R_SamplerInfo* sampler_info, String8 texture_path,
                     R_PipelineUsageType pipeline_usage_type);
static r_TextureInfo
vk_texture_info_get(Arena* arena, Buffer<U8> tex_data);

g_internal R_Handle
R_BufferLoad(R_BufferInfo* buffer_info);
