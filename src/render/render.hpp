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

struct MeshHandle
{
    Handle vertex_buffer_handle;
    Handle index_buffer_handle;
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
typedef void (*ThreadGpuWorkDoneFunc)(ThreadWorkerCmdCtx* thread_input);
struct ThreadWorkerCmdCtx
{
    Arena* arena;
    async::ThreadPool* thread_pool;
    render::HandleList handles;

    void* cmd_buffer;
    void* user_data;
    ThreadLoadingFunc loading_func;
    ThreadGpuWorkDoneFunc gpu_work_done_func;
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
// Pipeline

enum class DepthCompare
{
    LessOrEqual,
    Always,
    Equal,
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

    BufferInfo(Buffer<U8> buffer, U64 type_size, U32 buffer_type, U32 elem_count)
        : buffer(buffer), type_size(type_size), buffer_type(buffer_type), elem_count(elem_count)
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

struct AgentModelInfo
{
    render::Handle vertex_handle;
    render::Handle index_handle;
    U32 meshlet_count;
    U32 texture_handle_idx;
    glm::vec4 color;
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

enum class TilePipelineBits
{
    None = 0,
    ColorDisable = 1 << 0,
    DepthWriteDisable = 1 << 1,
    OverlayEnabled = 1 << 2,
    IsMapTile = 1 << 3,
    ColormapEnable = 1 << 4
};

constexpr bool
enable_bitmask(TilePipelineBits)
{
    return true;
}

struct TileVertex
{
    glm::vec3 pos;
    glm::vec2 uv;
    glm::vec2 overlay_uv;
    U32 road_segment_idx;
};

struct TilePipelineData
{
    Handle vertex_buffer_render_handle;
    Handle index_buffer_render_handle;
    Handle road_segment_buffer_handle;
    Handle texture_handle;
    Handle overlay_texture_handle;
    Handle colormap_handle;

    Vec2F32 overlay_translation;
    Vec2F32 overlay_scale;
    S32 overlay_texture_coordinate_id;

    U32 index_count;
    U32 index_offset;

    F32 height_offset;
    F32 lod_fade;
    B32 road_test_enabled;

    TilePipelineBits pipeline_bits;
    DepthCompare depth_test_compare;

    U32 overlay_option_idx;
};

struct TilePipelineDataNode
{
    TilePipelineData handles;
    TilePipelineDataNode* next;
};

struct TilePipelineDataList
{
    TilePipelineDataNode* first;
    TilePipelineDataNode* last;
};

struct Blend3DPipelineData
{
    Handle vertex_buffer_handle;
    Handle index_buffer_handle;
    Handle texture_handle;
    Handle colormap_handle;
    MappedHandle<void> camera_handle;
};

struct TileMesh
{
    Buffer<TileVertex> vertices;
    Buffer<U32> indices;
};

struct PrimitiveVertex
{
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec4 color;
    glm::vec2 uv;
};

struct PrimitiveMesh
{
    Buffer<PrimitiveVertex> vertices;
    Buffer<U16> indices;
};

struct Vertex3DBlend
{
    Vec3F32 pos;
    Vec2F32 uv;
    Vec2U32 object_id;
    Vec2F32 blend_factor;
};

struct Transform
{
    glm::vec4 x_basis;
    glm::vec4 y_basis;
    glm::vec4 z_basis;
    glm::vec4 w_basis;
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

struct Line
{
    glm::vec3 from;
    glm::vec3 to;
    glm::vec3 color;
};

struct LineVertex
{
    glm::vec3 pos;
    glm::vec3 color;
};

struct PrimitiveInstance
{
    Buffer<glm::vec3> locations;
    F32 scale_factor;
    MeshHandle mesh_handle;
};

struct MeshletMeshHandle
{
    Handle vertex_buffer_handle;
    Handle meshlet_buffer_handle;
    U32 meshlet_count; // Maximum meshlets in any LOD, used to size task dispatches.
};

// Mesh handles must be created by mesh_shader_handles_create_and_upload.
// Uses PrimitiveVertex meshes and one non-singular, orientation-preserving
// affine model-to-world transform per instance (translation, rotation, scale).
struct MeshInstanceBatch
{
    Buffer<Transform> transforms;
    MeshletMeshHandle mesh_handle;
    Handle texture_handle;
    B32 textured;
    F32 lod_error_pixels; // Zero keeps full detail; otherwise maximum projected simplification error.
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
render_frame(Vec2U32 framebuffer_dim, B32* in_out_framebuffer_resized, Vec2S64 mouse_cursor_pos,
             MappedHandle<void> camera_handle_void);

static void
gpu_work_update();
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
tile_pipeline_add(render::TilePipelineData* pipeline_input);
g_internal Handle
buffer_load_async(BufferInfo* buffer_info);

g_internal Handle
_buffer_load_immediate(render::BufferInfo* buffer_info, String8 debug_name);
#if BUILD_DEBUG
#define buffer_load_immediate(buffer_info, name) _buffer_load_immediate(buffer_info, name)
#else
#define buffer_load_immediate(buffer_info, name) _buffer_load_immediate(buffer_info, S(""))
#endif

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

// handle helpers

g_internal render::MeshHandle
mesh_handles_create_and_upload(render::PrimitiveMesh& prim_mesh);
g_internal MeshletMeshHandle
mesh_shader_handles_create_and_upload(PrimitiveMesh& mesh);
g_internal MeshletMeshHandle
mesh_shader_handles_create_and_upload(Buffer<PrimitiveVertex> vertices, Buffer<U32> indices, Arena* source_arena = 0);
g_internal void
mesh_shader_handles_destroy(MeshletMeshHandle mesh);
} // namespace render
