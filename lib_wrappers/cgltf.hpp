#include "third_party/cgltf.h"
// forward declaration
namespace city
{
struct CarVertex;
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
    Buffer<city::CarVertex> vertex_buffer;
    Buffer<U32> index_buffer;
    CgltfSampler sampler;
};
static VkSamplerCreateInfo
VkSamplerFromCgltfSampler(CgltfSampler sampler);
static CgltfResult
CgltfParse(Arena* arena, String8 gltf_path);
} // namespace wrapper
