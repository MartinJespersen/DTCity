#include "third_party/cgltf.h"
// forward declaration
namespace city
{
struct Vertex3D;
}

namespace wrapper
{
struct CgltfSampler
{
    cgltf_filter_type mag_filter;
    cgltf_filter_type min_filter;
    cgltf_wrap_mode wrap_s;
    cgltf_wrap_mode wrap_t;
};

struct CgltfResult
{
    Buffer<city::Vertex3D> vertex_buffer;
    Buffer<U32> index_buffer;
    CgltfSampler sampler;
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
    Buffer<city::Vertex3D> vertex_buffer;
    Buffer<U32> index_buffer;
};

static CgltfNode*
ChildrenNodesDepthFirstPreOrder(Arena* arena, CgltfNode** stack, CgltfNode* node);
static R_SamplerInfo
SamplerFromCgltfSampler(CgltfSampler sampler);
static CgltfResult
CgltfParse(Arena* arena, String8 gltf_path, String8 root_node_name);
} // namespace wrapper
