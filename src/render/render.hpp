#pragma once

////////////////////////////////
//~ mgj: Handle Type
union r_Handle
{
    void* ptr;
    U64 u64;
    U32 u32[2];
    U16 u16[4];
    static_assert(sizeof(ptr) == 8, "ptr should be 8 bytes");
};

/////////////////////////////////
enum r_ResourceKind
{
    R_ResourceKind_Static,
    R_ResourceKind_Dynamic,
    R_ResourceKind_Stream,
    R_ResourceKind_COUNT,
};

enum r_Tex2DFormat
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
enum r_MipMapMode
{
    R_MipMapMode_Nearest = 0,
    R_MipMapMode_Linear = 1,
};

enum r_Filter
{
    R_Filter_Nearest = 0,
    R_Filter_Linear = 1,
};

enum r_SamplerAddressMode
{
    R_SamplerAddressMode_Repeat,
    R_SamplerAddressMode_MirroredRepeat,
    R_SamplerAddressMode_ClampToEdge,
    R_SamplerAddressMode_ClampToBorder,
};

struct r_SamplerInfo
{
    r_Filter min_filter;
    r_Filter mag_filter;
    r_MipMapMode mip_map_mode;
    r_SamplerAddressMode address_mode_u;
    r_SamplerAddressMode address_mode_v;
};

enum r_BufferType
{
    R_BufferType_Invalid,
    R_BufferType_Vertex,
    R_BufferType_Index
};

struct r_BufferInfo
{
    Buffer<U8> buffer;
    U64 type_size;
    r_BufferType buffer_type;
};

enum r_AssetItemType
{
    R_AssetItemType_Undefined,
    R_AssetItemType_Texture,
    R_AssetItemType_Buffer
};

enum r_PipelineUsageType
{
    R_PipelineUsageType_Invalid,
    R_PipelineUsageType_3D,
    R_PipelineUsageType_3DInstanced,
};

template <typename T> struct r_AssetItem
{
    r_AssetItem* next;
    B32 is_loaded;
    T item;
};

template <typename T> struct r_AssetItemList
{
    r_AssetItem<T>* first;
    r_AssetItem<T>* last;
};

struct r_TextureLoadingInfo
{
    String8 tex_path;
};

struct r_AssetLoadingInfo
{
    r_Handle handle;
    r_AssetItemType type;
    union
    {
        r_TextureLoadingInfo texture_info;
        r_BufferInfo buffer_info;
    } extra_info;
};

struct r_ThreadInput
{
    Arena* arena;
    r_AssetLoadingInfo asset_info;
};

struct r_Model3DPipelineData
{
    r_Handle vertex_buffer_handle;
    r_Handle index_buffer_handle;
    r_Handle texture_handle;

    U64 index_count;
    U32 index_offset;
};

struct r_Model3DPipelineDataNode
{
    r_Model3DPipelineData handles;
    r_Model3DPipelineDataNode* next;
};

struct r_Model3DPipelineDataList
{
    r_Model3DPipelineDataNode* first;
    r_Model3DPipelineDataNode* last;
};

struct r_Vertex3D
{
    Vec3F32 pos;
    Vec2F32 uv;
    Vec2U32 object_id;
};

struct r_Model3DInstance
{
    glm::vec4 x_basis;
    glm::vec4 y_basis;
    glm::vec4 z_basis;
    glm::vec4 w_basis;
};

static r_Handle
r_handle_zero();

template <typename T>
static r_BufferInfo
r_buffer_info_from_template_buffer(Buffer<T> buffer, r_BufferType buffer_type);

//////////////////////////////////////////////////////////////////////////
// ~mgj: function declaration to be implemented by backend

static void
r_render_ctx_create(String8 shader_path, io_IO* io_ctx, async::Threads* thread_pool);
static void
r_render_ctx_destroy();
static void
r_render_frame(Vec2U32 framebuffer_dim, B32* in_out_framebuffer_resized, ui_Camera* camera,
               Vec2S64 mouse_cursor_pos);
static void
r_gpu_work_done_wait();
static void
r_new_frame();
static U64
r_latest_hovered_object_id_get();

// ~mgj: Texture loading interface
g_internal r_Handle
r_texture_handle_create(r_SamplerInfo* sampler_info, r_PipelineUsageType pipeline_usage_type);
g_internal r_Handle
r_texture_load_async(r_SamplerInfo* sampler_info, String8 texture_path,
                     r_PipelineUsageType pipeline_usage_type);
g_internal void
r_texture_gpu_upload_sync(r_Handle tex_handle, Buffer<U8> tex_bufs);

g_internal void
r_texture_destroy(r_Handle handle);
g_internal void
r_buffer_destroy(r_Handle handle);

g_internal void
r_model_3d_draw(r_Model3DPipelineData pipeline_input, B32 depth_test_per_draw_call_only);

g_internal r_Handle
r_buffer_load(r_BufferInfo* buffer_info);
