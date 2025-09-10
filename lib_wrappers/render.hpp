#pragma once
namespace render
{

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
};

struct BufferInfo
{
    Buffer<U8> buffer;
    U64 type_size;
};
struct AssetId
{
    U64 id;
};
enum AssetItemType
{
    AssetItemType_Undefined,
    AssetItemType_Texture,
    AssetItemType_Buffer
};

enum PipelineUsageType
{
    PipelineUsageType_Undefined,
    PipelineUsageType_VertexBuffer,
    PipelineUsageType_IndexBuffer,
    PipelineUsageType_3D,
    PipelineUsageType_3DInstanced,
};

template <typename T> struct AssetItem
{
    AssetItem* next;
    AssetId id;
    B32 is_loaded;
    B32 is_loading;
    T item;
};

template <typename T> struct AssetItemList
{
    AssetItem<T>* first;
    AssetItem<T>* last;
};

struct AssetInfo
{
    AssetId id;
    AssetItemType type;
    PipelineUsageType pipeline_usage_type;
};

struct TextureLoadingInfo
{
    String8 texture_path;
    render::SamplerInfo sampler_info;
};

struct AssetLoadingInfo
{
    AssetInfo info;
    B32 is_loaded;
    union
    {
        TextureLoadingInfo texture_info;
        render::BufferInfo buffer_info;
    } extra_info;
};

struct AssetLoadingInfoNode
{
    AssetLoadingInfoNode* next;
    AssetLoadingInfo load_info;
};

struct AssetLoadingInfoNodeList
{
    AssetLoadingInfoNode* first;
    AssetLoadingInfoNode* last;
    U64 count;
};

struct ThreadInput
{
    Arena* arena;
    AssetLoadingInfoNodeList asset_loading_wait_list;
};
template <typename T>
static BufferInfo
BufferInfoFromTemplateBuffer(Buffer<T> buffer);
static AssetId
AssetIdFromStr8(String8 str);
static AssetInfo
AssetInfoCreate(String8 name, AssetItemType type, PipelineUsageType pipeline_usage_type);
} // namespace render
