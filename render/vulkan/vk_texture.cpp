

// ~mgj: Handle Conversions
static VK_Tex2D*
VK_TextureFromHandle(R_Handle handle)
{
    return (VK_Tex2D*)handle.u64[0];
}

// static R_AssetItem<Texture>*
// AssetManagerTextureItemGet(R_AssetId asset_id)
// {
//     ScratchScope scratch = ScratchScope(0, 0);
//     VulkanContext* vk_ctx = VulkanCtxGet();
//     AssetManager* asset_store = vk_ctx->asset_manager;

//     R_AssetItemList<Texture>* texture_list =
//         &asset_store->texture_hashmap
//              .data[HashIndexFromAssetId(asset_id, asset_store->texture_hashmap.size)];

//     return AssetManagerItemGet(asset_store->texture_arena, texture_list,
//                                &asset_store->texture_free_list, asset_id);
// }

// static R_AssetItem<AssetItemBuffer>*
// AssetManagerBufferItemGet(R_AssetId asset_id)
// {
//     VulkanContext* vk_ctx = VulkanCtxGet();
//     AssetManager* asset_store = vk_ctx->asset_manager;

//     return AssetManagerItemGet(asset_store->buffer_arena, &asset_store->buffer_list,
//                                &asset_store->buffer_free_list, asset_id);
// }

static VK_Tex2D*
VK_TextureGet(R_Handle handle)
{
    for (VK_Tex2D* tex = r_tex2d_list.first; tex; tex = tex->next)
    {
        if (MemoryCompare(&handle, &tex, sizeof(U64)))
        {
            return tex;
        }
    }
    R_AssetItem<T>* asset_item = {0};
    if (*free_list)
    {
        asset_item = *free_list;
        SLLStackPop(asset_item);
    }
    else
    {
        asset_item = PushStruct(arena, R_AssetItem<T>);
        asset_item->id = id;
        SLLQueuePushFront(list->first, list->last, asset_item);
    }
    return asset_item;
}
