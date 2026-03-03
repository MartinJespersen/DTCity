#pragma once

namespace render
{

////////////////////////////////
//~ mgj: Handle Types
enum class HandleType : U32
{
    Undefined,
    Texture,
    Buffer,
    DescriptorSet
};

struct Handle
{
    union
    {
        void* ptr;
        U64 u64;
        U32 u32[2];
        U16 u16[4];
        static_assert(sizeof(ptr) == 8, "ptr should be 8 bytes");
    };
    U64 gen_id;
    HandleType type;

    Handle() : ptr(nullptr), gen_id(0), type(HandleType::Undefined)
    {
    }

    Handle(void* ptr, U64 gen_id, HandleType asset_type)
        : ptr(ptr), gen_id(gen_id), type(asset_type)
    {
    }

    static Handle
    texture_handle_create();

    static Handle
    buffer_handle_create();

    static Handle
    descriptor_set_handle_create();
};

struct HandleNode
{
    HandleNode* next;
    render::Handle handle;
};

struct HandleList
{
    render::HandleNode* first;
    render::HandleNode* last;
    U32 count;
};

////////////////////////////////
struct ThreadInput;
typedef void (*ThreadLoadingFunc)(void* data, ThreadInput* thread_input);
typedef void (*ThreadDoneLoadingFunc)(HandleList handles);
struct ThreadInput
{
    Arena* arena;
    render::HandleList handles;

    void* cmd_buffer;
    void* user_data;
    ThreadLoadingFunc loading_func;
    ThreadDoneLoadingFunc done_loading_func;
};
/////////////////////////////////

typedef void* FuncData;
typedef void* (*Func)(ThreadInput* thread_input, FuncData data);
struct ThreadSyncCallback
{
    FuncData data;
    Func function;

    ThreadSyncCallback(FuncData data, Func func) : data(data), function(func)
    {
    }
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

enum BufferType : U32
{
    BufferType_Invalid = 0,
    BufferType_Vertex = (1 << 0),
    BufferType_Index = (1 << 2),
    BufferType_Uniform = (1 << 3),
    BufferType_StorageBuffer = (1 << 4)
};

struct BufferInfo
{
    Buffer<U8> buffer;
    U64 type_size;
    U32 buffer_type;

    BufferInfo(Buffer<U8> buffer, U64 type_size, U32 buffer_type)
        : buffer(buffer), type_size(type_size), buffer_type(buffer_type)
    {
    }

    template <typename T> BufferInfo(Buffer<T> buffer, U32 buffer_type);

    template <typename T> BufferInfo(Arena* arena, T* buffer, U32 buffer_type);
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

    HandleType type;
    U64 gen_id;
    B32 is_loaded;
    T item;
};

template <typename T> struct AssetItemList
{
    AssetItem<T>* first;
    AssetItem<T>* last;
    U32 count;
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

    Handle storage_buffer_handle;

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
    F32 padding;
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

struct TextureUploadData
{
    U32 width;
    U32 height;
    U32 num_channels;
    U32 bytes_per_channel;
    U8* data;
    U32 data_byte_size;

    static TextureUploadData
    init(U8* data, U32 width, U32 height, U32 num_channels, U32 bytes_per_channel,
         U32 data_byte_size)
    {
        TextureUploadData result = {};
        result.width = width;
        result.height = height;
        result.num_channels = num_channels;
        result.bytes_per_channel = bytes_per_channel;
        result.data = data;
        result.data_byte_size = data_byte_size;
        return result;
    }

    static TextureUploadData
    init(U8* data, U32 width, U32 height, U32 num_channels, U32 bytes_per_channel)
    {
        TextureUploadData result = {};
        result.width = width;
        result.height = height;
        result.num_channels = num_channels;
        result.bytes_per_channel = bytes_per_channel;
        result.data = data;
        result.data_byte_size = width * height * num_channels * bytes_per_channel;
        return result;
    }
};

static ThreadInput*
thread_input_create();
static void
thread_input_destroy(ThreadInput* thread_input);

static Handle
handle_zero();
static bool
is_handle_zero(Handle handle);
static void
handle_list_push(Arena* arena, HandleList* list, Handle handle);
static Handle
handle_list_first_handle(HandleList* list);

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
g_internal render::Handle
texture_load_async(render::SamplerInfo* sampler_info, TextureUploadData* tex_upload_info);
static render::Handle
colormap_load_async(render::SamplerInfo* sampler_info, const U8* colormap_data, U64 colormap_size);
g_internal void
texture_gpu_upload_sync(Handle tex_handle, Buffer<U8> tex_bufs);

g_internal Handle
texture_load_sync(render::SamplerInfo* sampler_info, TextureUploadData* tex_data,
                  VkCommandBuffer cmd);
g_internal void
handle_destroy(Handle handle);
g_internal void
handle_destroy_deferred(Handle handle);

g_internal void
handle_done_loading(render::HandleList handles);

g_internal void
model_3d_draw(Model3DPipelineData pipeline_input);

g_internal void
blend_3d_draw(Blend3DPipelineData pipeline_input);

g_internal bool
road_intersection_compute_add(Handle storage_buffer_handle, Handle index_buffer_handle,
                              Handle road_segment_buffer_handle,
                              Handle road_segment_node_buffer_handle, Handle road_segment_handle);

g_internal Handle
buffer_load_async(BufferInfo* buffer_info);

g_internal Handle
buffer_load_sync(VkCommandBuffer cmd, render::BufferInfo* buffer_info);

g_internal Handle
uniform_buffer_load_sync(Arena* arena, Handle handle);

g_internal Handle
storage_buffer_load_sync(Arena* arena, Handle vertex_buffer_handle, Handle index_buffer_handle);

g_internal Handle
road_segment_descriptor_load_async(Arena* arena, Handle buffer_handle, Handle node_buffer_handle);

g_internal bool
is_resource_loaded(Handle handle);

g_internal void
model_3d_instance_draw(Handle texture_handle, Handle vertex_buffer_handle,
                       Handle index_buffer_handle, BufferInfo* instance_buffer);

g_internal void*
thread_cmd_buffer_record(ThreadInput* thread_input, ThreadSyncCallback callback);
} // namespace render
