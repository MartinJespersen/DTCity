////////////////////////////////////////////////////////
// ~mgj: Handles
static R_Handle
R_HandleZero()
{
    R_Handle handle = {};
    return handle;
}

////////////////////////////////////////////////////////
template <typename T>
static R_BufferInfo
R_BufferInfoFromTemplateBuffer(Buffer<T> buffer)
{
    U64 type_size = sizeof(T);
    U64 byte_count = buffer.size * type_size;
    Buffer<U8> general_buffer = {.data = (U8*)buffer.data, .size = byte_count};
    return {.buffer = general_buffer, .type_size = type_size};
}
static R_AssetId
R_AssetIdFromStr8(String8 str)
{
    return {.id = HashU128FromStr8(str).u64[0]};
}
static R_AssetInfo
R_AssetInfoCreate(String8 name, R_AssetItemType type, R_PipelineUsageType pipeline_usage_type)
{
    R_AssetInfo asset_info = {};
    asset_info.id = R_AssetIdFromStr8(name);
    asset_info.type = type;
    asset_info.pipeline_usage_type = pipeline_usage_type;
    return asset_info;
}
