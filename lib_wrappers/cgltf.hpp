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
static render::SamplerInfo
SamplerFromCgltfSampler(CgltfSampler sampler);
static CgltfResult
CgltfParse(Arena* arena, String8 gltf_path);
} // namespace wrapper
