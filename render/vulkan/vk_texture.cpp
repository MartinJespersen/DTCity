

// ~mgj: Handle Conversions
static VK_Tex2D*
VK_Tex2DFromHandle(R_Handle handle)
{
    return (VK_Tex2D*)handle.u64[0];
}

static R_Handle
VK_Tex2DToHandle(VK_Tex2D* vk_tex2d)
{
    R_Handle handle = {.u64 = (U64)vk_tex2d};
    return handle;
}
static B32
VK_TextureGet(R_Handle handle, VK_Tex2D* out_tex)
{
    B32 not_found = TRUE;
    for (VK_Tex2D* tex = r_tex2d_list.first; tex; tex = tex->next)
    {
        if (MemoryCompare(&handle, &tex, sizeof(U64)))
        {
            out_tex = tex;
            not_found = FALSE;
        }
    }
    return not_found;
}
