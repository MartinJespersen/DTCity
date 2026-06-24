#pragma once
// TODO: IO should not be a dependency of this layer
namespace io
{
struct IO;
};
namespace render
{

static const U32 MAX_FRAMES_IN_FLIGHT = 2;
////////////////////////////////
//~ mgj: Handle Types

enum class HandleType : S32
{
    Undefined,
    Texture,
    Buffer
};

enum BufferType : U32
{
    BufferType_Invalid = 0,
    BufferType_Vertex = (1 << 0),
    BufferType_Index = (1 << 2),
    BufferType_Uniform = (1 << 3),
    BufferType_StorageBuffer = (1 << 4),
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

    Handle(void* ptr, U64 gen_id, HandleType asset_type) : ptr(ptr), gen_id(gen_id), type(asset_type)
    {
    }

    static Handle
    texture_handle_create();

    static Handle
    buffer_handle_create(BufferType buffer_type);
};

template <typename T>
struct MappedHandleFrame
{
    T* data;
    render::Handle handle;
};

template <typename T>
struct MappedHandle
{
    Buffer<MappedHandleFrame<T>> buffer;
};

template <typename T>
static MappedHandle<void>
mapped_handle_erased(MappedHandle<T> handle)
{
    MappedHandle<void> result = {};
    result.buffer.data = (MappedHandleFrame<void>*)handle.buffer.data;
    result.buffer.size = handle.buffer.size;
    return result;
}

struct HandleNode
{
    HandleNode* next;
    B32 work_on_gpu_done;
    render::Handle handle;
};

struct HandleList
{
    render::HandleNode* first;
    render::HandleNode* last;
    U32 count;
};

////////////////////////////////
struct ThreadWorkerCmdCtx;
typedef void (*ThreadLoadingFunc)(void* data, ThreadWorkerCmdCtx* thread_input);
typedef void (*ThreadDoneLoadingFunc)(HandleList handles);
struct ThreadWorkerCmdCtx
{
    Arena* arena;
    async::ThreadPool* thread_pool;
    render::HandleList handles;

    void* cmd_buffer;
    void* user_data;
    ThreadLoadingFunc loading_func;
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

struct BufferInfo
{
    Buffer<U8> buffer;
    U64 type_size;
    U32 buffer_type;
    U32 elem_count;

    BufferInfo(Buffer<U8> buffer, U64 type_size, U32 buffer_type, U32 elem_count) : buffer(buffer), type_size(type_size), buffer_type(buffer_type), elem_count(elem_count)
    {
    }

    template <typename T>
    BufferInfo(Buffer<T> buffer, U32 buffer_type);

    template <typename T>
    BufferInfo(Arena* arena, T* buffer, U32 buffer_type);

    template <typename T>
    static BufferInfo
    empty_buffer_info(Arena* arena, BufferType buffer_type);

    BufferInfo
    copy_to_arena(Arena* arena);
};

enum class PipelineLayoutType
{
    Invalid,
    Model3D,
    Model3DInstance,
    Blend3D_Tex,
    Blend3D_ColorMap
};

template <typename T>
struct AssetItem
{
    AssetItem* next;
    AssetItem* prev;

    HandleType type;
    U64 gen_id;
    B32 is_loaded;
    T item;
};

template <typename T>
struct AssetItemList
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
    Handle overlay_texture_handle;
    Handle colormap_handle;
    MappedHandle<void> camera_handle;

    Vec2F32 overlay_translation;
    Vec2F32 overlay_scale;
    S32 overlay_texture_coordinate_id;
    B32 has_overlay_uv;

    U64 index_count;
    U32 index_offset;

    Vec2F32 bbox_min;
    Vec2F32 bbox_max;

    B32 is_map_tile;
    F32 height_offset;
    F32 depth_bias;
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
    Handle camera_handle;
};

struct TileVertex
{
    Vec3F32 pos;
    F32 colormap_value;
    Vec2F32 uv;
    Vec2F32 overlay_uv;
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

struct MeshHandlePair
{
    Handle vertex_handle;
    Handle index_handle;
    U32 texture_handle_idx;
};

struct Quad2F64
{
    glm::vec3 btm_lt_pos;
    glm::vec2 size;
    F64 height;
};

struct BBoxDraw
{
    Arena* arena;
    Handle tex;
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
    init(U8* data, U32 width, U32 height, U32 num_channels, U32 bytes_per_channel, U32 data_byte_size)
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

g_internal void
thread_cmd_buffer_end(ThreadWorkerCmdCtx* cmd_ctx);
g_internal void
thread_cmd_buffer_record(ThreadWorkerCmdCtx* thread_ctx);

static ThreadWorkerCmdCtx*
thread_ctx_create();
static void
thread_input_destroy(ThreadWorkerCmdCtx* thread_input);

static Handle
handle_zero();
static bool
is_handle_zero(Handle handle);
static void
handle_list_push(ThreadWorkerCmdCtx* thread_ctx, render::Handle handle);
static Handle
handle_list_first_handle(HandleList* list);

//////////////////////////////////////////////////////////////////////////
// ~mgj: function declaration to be implemented by backend

static void
render_ctx_create(String8 shader_path, io::IO* io_ctx, async::ThreadPool* thread_pool);
static void
render_ctx_destroy();
static void
render_frame(Vec2U32 framebuffer_dim, B32* in_out_framebuffer_resized, Vec2S64 mouse_cursor_pos);

static void
gpu_work_done_wait();
static void
new_frame();
static U64
latest_hovered_object_id_get();

// ~mgj: Texture loading interface
g_internal Handle
texture_zero_handle_get();
g_internal Handle
texture_handle_create(SamplerInfo* sampler_info);
g_internal Handle
texture_load_async(SamplerInfo* sampler_info, String8 texture_path);

static render::Handle
colormap_load_async(render::SamplerInfo* sampler_info, const U8* colormap_data, U64 colormap_size);
g_internal render::Handle
colormap_load_sync(render::ThreadWorkerCmdCtx* thread_ctx, render::SamplerInfo* sampler_info, const U8* colormap_data, U64 colormap_size);

g_internal Handle
texture_load_sync(render::SamplerInfo* sampler_info, TextureUploadData* tex_data, void* cmd);
g_internal Handle
texture_load_sync(render::ThreadWorkerCmdCtx* thread_ctx, render::SamplerInfo* sampler_info, Buffer<U8> tex_buf);
g_internal void
handle_destroy(Handle handle);
g_internal void
handle_destroy_deferred(Handle handle);

g_internal void
handle_done_loading(render::HandleList handles);

g_internal void
blend_3d_draw(Blend3DPipelineData pipeline_input);

static void
model_3d_bucket_add(render::Model3DPipelineData* pipeline_input);
g_internal bool
agent_instance_render_bucket_add(render::MappedHandle<void> camera_handle, Buffer<render::MeshHandlePair> meshes, Buffer<render::Handle> texture_handles, render::BufferInfo* instance_buffer_info,
                                 U32 instance_buffer_offset);

g_internal void
agent_instance_compute_bucket_add(render::BufferInfo* instance_buffer_info, render::Handle tile_vertex_buffer_handle, render::Handle tile_index_buffer_handle, F32 car_center_to_road_offset,
                                  U32 instance_buffer_offset);

g_internal bool
road_intersection_compute_add(Handle vertex_buffer_handle, Handle index_buffer_handle, Handle road_segment_buffer_handle, Handle road_segment_node_buffer_handle, U32 overlay_option);

g_internal Handle
buffer_load_async(BufferInfo* buffer_info);

g_internal Handle
_buffer_load_sync(render::ThreadWorkerCmdCtx* thread_ctx, render::BufferInfo* buffer_info, String8 debug_name);
#if BUILD_DEBUG
#define buffer_load_sync(thread_ctx, buffer_info, name) _buffer_load_sync(thread_ctx, buffer_info, name)
#else
#define buffer_load_sync(thread_ctx, buffer_info, name) _buffer_load_sync(thread_ctx, buffer_info, S(""))
#endif

template <typename T>
g_internal MappedHandle<T>
mapped_buffer_create(Arena* arena, render::ThreadWorkerCmdCtx* thread_ctx, BufferType buffer_type, String8 debug_name);

template <typename T>
g_internal void
mapped_buffer_destroy(MappedHandle<T> mapped_handle);
template <typename T>
g_internal void
mapped_buffer_add(MappedHandle<T> mut_handle, T* data);

template <typename T>
g_internal bool
is_resource_loaded(Handle handle, AssetItem<T>** out_asset);
g_internal bool
is_resource_loaded(Handle handle);

} // namespace render
