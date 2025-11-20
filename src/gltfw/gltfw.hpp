#include "third_party/cgltf.h"

struct gltfw_Vertex3D
{
    Vec3F32 pos;
    Vec2F32 uv;
    Vec2U32 object_id;
};

struct gltfw_Sampler
{
    cgltf_filter_type mag_filter;
    cgltf_filter_type min_filter;
    cgltf_wrap_mode wrap_s;
    cgltf_wrap_mode wrap_t;
};

struct CgltfResult
{
    Buffer<gltfw_Vertex3D> vertex_buffer;
    Buffer<U32> index_buffer;
    gltfw_Sampler sampler;
};

struct CgltfNode
{
    CgltfNode* next;
    cgltf_node* node;
    U32 cur_child_index;
};

struct BufferNode
{
    BufferNode* next;
    Buffer<gltfw_Vertex3D> vertex_buffer;
    Buffer<U32> index_buffer;
};

struct gltfw_Primitive
{
    gltfw_Primitive* next;
    Buffer<gltfw_Vertex3D> vertices;
    Buffer<U32> indices;
    U32 tex_idx;
};

struct gltfw_PrimitiveList
{
    gltfw_Primitive* first;
    gltfw_Primitive* last;
};

struct gltfw_Texture
{
    Buffer<U8> tex_buf;
    gltfw_Sampler sampler;
};

struct gltfw_Result
{
    gltfw_PrimitiveList primitives;
    Buffer<gltfw_Texture> samplers;
};

static CgltfNode*
ChildrenNodesDepthFirstPreOrder(Arena* arena, CgltfNode** stack, CgltfNode* node);
static CgltfResult
gltfw_gltf_read(Arena* arena, String8 gltf_path, String8 root_node_name);
static gltfw_Result
gltfw_glb_read(Arena* arena, String8 glb_path);
