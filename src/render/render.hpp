#pragma once

namespace render
{

////////////////////////////////
//~ mgj: Handle Type
union Handle
{
    void* ptr;
    U64 u64;
    U32 u32[2];
    U16 u16[4];
    static_assert(sizeof(ptr) == 8, "ptr should be 8 bytes");
};

/////////////////////////////////
enum ResourceKind
{
    ResourceKind_Static,
    ResourceKind_Dynamic,
    ResourceKind_Stream,
    ResourceKind_COUNT,
};

enum Tex2DFormat
{
    Tex2DFormat_R8,
    Tex2DFormat_RG8,
    Tex2DFormat_RGBA8,
    Tex2DFormat_BGRA8,
    Tex2DFormat_R16,
    Tex2DFormat_RGBA16,
    Tex2DFormat_R32,
    Tex2DFormat_RG32,
    Tex2DFormat_RGBA32,
    Tex2DFormat_COUNT,
};

// ~mgj: Sampler
enum MipMapMode
{
    MipMapMode_Nearest = 0,
    MipMapMode_Linear = 1,
};

enum Filter
{
    Filter_Nearest = 0,
    Filter_Linear = 1,
};

enum SamplerAddressMode
{
    SamplerAddressMode_Repeat,
    SamplerAddressMode_MirroredRepeat,
    SamplerAddressMode_ClampToEdge,
    SamplerAddressMode_ClampToBorder,
};

struct SamplerInfo
{
    Filter min_filter;
    Filter mag_filter;
    MipMapMode mip_map_mode;
    SamplerAddressMode address_mode_u;
    SamplerAddressMode address_mode_v;
    bool unnormalized_coordinates;
};

enum BufferType
{
    BufferType_Invalid,
    BufferType_Vertex,
    BufferType_Index
};

struct BufferInfo
{
    Buffer<U8> buffer;
    U64 type_size;
    BufferType buffer_type;
};

enum AssetItemType
{
    AssetItemType_Undefined,
    AssetItemType_Texture,
    AssetItemType_Buffer
};

enum class PipelineLayoutType
{
    Invalid,
    Model3D,
    Model3DInstance,
    Blend3D_Tex,
    Blend3D_ColorMap

};

template <typename T> struct AssetItem
{
    AssetItem* next;
    AssetItem* prev;
    B32 is_loaded;
    T item;
};

template <typename T> struct AssetItemList
{
    AssetItem<T>* first;
    AssetItem<T>* last;
};

struct TextureLoadingInfo
{
    String8 tex_path;
};

struct ColorMapLoadingInfo
{
    const U8* colormap_data;
    U64 colormap_size;
};

struct Model3DPipelineData
{
    Handle vertex_buffer_handle;
    Handle index_buffer_handle;
    Handle texture_handle;

    U64 index_count;
    U32 index_offset;
};

struct Model3DPipelineDataNode
{
    Model3DPipelineData handles;
    Model3DPipelineDataNode* next;
};

struct Model3DPipelineDataList
{
    Model3DPipelineDataNode* first;
    Model3DPipelineDataNode* last;
};

struct Blend3DPipelineData
{
    Handle vertex_buffer_handle;
    Handle index_buffer_handle;
    Handle texture_handle;
    Handle colormap_handle;
};

struct Vertex3D
{
    Vec3F32 pos;
    Vec2F32 uv;
    Vec2U32 object_id;
};

struct Vertex3DBlend
{
    Vec3F32 pos;
    Vec2F32 uv;
    Vec2U32 object_id;
    Vec2F32 blend_factor;
};

struct Model3DInstance
{
    glm::vec4 x_basis;
    glm::vec4 y_basis;
    glm::vec4 z_basis;
    glm::vec4 w_basis;
};

static Handle
handle_zero();
static bool
is_handle_zero(Handle handle);
static BufferInfo
buffer_info_from_vertex_3d_buffer(Buffer<Vertex3D> buffer, BufferType buffer_type);

static BufferInfo
buffer_info_from_u32_index_buffer(Buffer<U32> buffer, BufferType buffer_type);

static BufferInfo
buffer_info_from_vertex_3d_instance_buffer(Buffer<Model3DInstance> buffer, BufferType buffer_type);

static render::BufferInfo
buffer_info_from_vertex_blend_3d_buffer(Buffer<Vertex3DBlend> buffer,
                                        render::BufferType buffer_type);

// privates
template <typename T>
static BufferInfo
buffer_info_from_template_buffer(Buffer<T> buffer, BufferType buffer_type, U64 type_size);

//////////////////////////////////////////////////////////////////////////
// ~mgj: function declaration to be implemented by backend

static void
render_ctx_create(String8 shader_path, io::IO* io_ctx, async::Threads* thread_pool);
static void
render_ctx_destroy();
static void
render_frame(Vec2U32 framebuffer_dim, B32* in_out_framebuffer_resized, ui::Camera* camera,
             Vec2S64 mouse_cursor_pos);
static void
gpu_work_done_wait();
static void
new_frame();
static U64
latest_hovered_object_id_get();

// ~mgj: Texture loading interface
g_internal Handle
texture_handle_create(SamplerInfo* sampler_info);
g_internal Handle
texture_load_async(SamplerInfo* sampler_info, String8 texture_path);
static render::Handle
colormap_load_async(render::SamplerInfo* sampler_info, const U8* colormap_data, U64 colormap_size);
g_internal void
texture_gpu_upload_sync(Handle tex_handle, Buffer<U8> tex_bufs);

g_internal void
texture_destroy(Handle handle);
g_internal void
buffer_destroy(Handle handle);

g_internal void
texture_destroy_deferred(Handle handle);
g_internal void
buffer_destroy_deferred(Handle handle);

g_internal void
model_3d_draw(Model3DPipelineData pipeline_input, B32 depth_test_per_draw_call_only);

g_internal void
blend_3d_draw(Blend3DPipelineData pipeline_input);

g_internal Handle
buffer_load(BufferInfo* buffer_info);

g_internal bool
is_resource_loaded(Handle handle);

g_internal void
model_3d_instance_draw(Handle texture_handle, Handle vertex_buffer_handle,
                       Handle index_buffer_handle, BufferInfo* instance_buffer);

} // namespace render
