

#pragma once

////////////////////////////////
//~ mgj: Handle Type
union R_Handle
{
    U64 u64[1];
    U32 u32[2];
    U16 u16[4];
};

/////////////////////////////////
enum R_ResourceKind
{
    R_ResourceKind_Static,
    R_ResourceKind_Dynamic,
    R_ResourceKind_Stream,
    R_ResourceKind_COUNT,
};

enum R_Tex2DFormat
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
enum R_MipMapMode
{
    R_MipMapMode_Nearest = 0,
    R_MipMapMode_Linear = 1,
};

enum R_Filter
{
    R_Filter_Nearest = 0,
    R_Filter_Linear = 1,
};

enum R_SamplerAddressMode
{
    R_SamplerAddressMode_Repeat,
    R_SamplerAddressMode_MirroredRepeat,
    R_SamplerAddressMode_ClampToEdge,
    R_SamplerAddressMode_ClampToBorder,
};

struct R_SamplerInfo
{
    R_Filter min_filter;
    R_Filter mag_filter;
    R_MipMapMode mip_map_mode;
    R_SamplerAddressMode address_mode_u;
    R_SamplerAddressMode address_mode_v;
};

struct R_BufferInfo
{
    Buffer<U8> buffer;
    U64 type_size;
};
struct R_AssetId
{
    U64 id;
};
enum R_AssetItemType
{
    R_AssetItemType_Undefined,
    R_AssetItemType_Texture,
    R_AssetItemType_Buffer
};

enum R_PipelineUsageType
{
    R_PipelineUsageType_Undefined,
    R_PipelineUsageType_VertexBuffer,
    R_PipelineUsageType_IndexBuffer,
    R_PipelineUsageType_3D,
    R_PipelineUsageType_3DInstanced,
};

template <typename T> struct R_AssetItem
{
    R_AssetItem* next;
    R_AssetId id;
    B32 is_loaded;
    B32 is_loading;
    T item;
};

template <typename T> struct R_AssetItemList
{
    R_AssetItem<T>* first;
    R_AssetItem<T>* last;
};

struct R_AssetInfo
{
    R_AssetId id;
    R_AssetItemType type;
    R_PipelineUsageType pipeline_usage_type;
};

struct R_TextureLoadingInfo
{
    String8 texture_path;
};

struct R_AssetLoadingInfo
{
    R_AssetInfo info;
    B32 is_loaded;
    union
    {
        R_TextureLoadingInfo texture_info;
        R_BufferInfo buffer_info;
    } extra_info;
};

struct R_AssetLoadingInfoNode
{
    R_AssetLoadingInfoNode* next;
    R_AssetLoadingInfo load_info;
};

struct R_AssetLoadingInfoNodeList
{
    R_AssetLoadingInfoNode* first;
    R_AssetLoadingInfoNode* last;
    U64 count;
};

struct R_ThreadInput
{
    Arena* arena;
    R_AssetLoadingInfoNodeList asset_loading_wait_list;
};
static R_Handle
R_HandleZero();

template <typename T>
static R_BufferInfo
R_BufferInfoFromTemplateBuffer(Buffer<T> buffer);
static R_AssetId
R_AssetIdFromStr8(String8 str);
static R_AssetInfo
R_AssetInfoCreate(String8 name, R_AssetItemType type, R_PipelineUsageType pipeline_usage_type);

//////////////////////////////////////////////////////////////////////////
// ~mgj: function declaration to be implemented by backend
static R_Handle
R_Tex2dAlloc(R_ResourceKind kind, Vec2S32 size, R_Tex2DFormat format, void* data);
static void
R_FillTex2dRegion(R_Handle handle, Rng2S32 subrect, void* data);
static void
R_Tex2dRelease(R_Handle texture);
