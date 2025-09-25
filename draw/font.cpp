namespace draw
{

// static Vec2F32
// TextDimensionsCalculate(String8 text)
// {
//     Vec2F32 dimensions = {0.0, 0.0};
//     U64 len = text.size;

//     for (U32 i = 0; i < len; i++)
//     {
//         if ((U32)text.str[i] >= font->MAX_GLYPHS)
//         {
//             exitWithError("Character not supported!");
//         }

//         Character* ch = &font->characters[(u64)text.str[i]];
//         dimensions.x += (f32)(ch->advance >> 6);
//         dimensions.y = Max((float)dimensions.y, ch->height);
//     }

//     return dimensions;
// }

// static void
// TextDraw(String8 text, Rng2F32 pos_rng)
// {
//     DrawCtx* ctx = DrawCtxGet();
//     Text* text_ctx = ctx->text;
//     Frame* frame = ctx->frame;
//     RectNodeList* rect_list = &frame->rect_list;

//     Vec2F32 p0 = pos_rng.p0;
//     Vec2F32 p1 = pos_rng.p1;

//     // find largest bearing to find origin
//     F32 largest_bearing_y = 0;
//     for (U32 text_idx = 0; text_idx < text.size; text_idx++)
//     {
//         Character* ch = &text_ctx->characters.data[(U64)text.str[text_idx]];
//         if (ch->bearing_y > largest_bearing_y)
//         {
//             largest_bearing_y = ch->bearing_y;
//         }
//     }

//     F32 xOrigin = p0.x;
//     F32 yOrigin = p0.y + largest_bearing_y;

//     for (U32 i = 0; i < text.size; i++)
//     {
//         if ((U32)text.str[i] >= text_ctx->max_glyphs)
//         {
//             exitWithError("Character not supported!");
//         }

//         Character* ch = &text_ctx->characters.data[(U64)text.str[i]];

//         F32 xGlyphPos0 = xOrigin + ch->bearing_x;
//         F32 yGlyphPos0 = yOrigin - ch->bearing_y;

//         F32 p0_x_offset = Max(p0.x - xGlyphPos0, 0.0f);

//         F32 xpos0 = Clamp(xGlyphPos0, p0.x, p1.x);
//         F32 ypos0 = Clamp(yGlyphPos0, p0.y, p1.y);
//         F32 xpos1 = Clamp(xGlyphPos0 + ch->width, p0.x, p1.x);
//         F32 ypos1 = Clamp(yGlyphPos0 + ch->height, p0.y, p1.y);

//         F32 text_height = p1.y - p0.y;
//         F32 p0_y_offset =
//             Max(-(largest_bearing_y - ch->bearing_y) + ((text_height - (ypos1 - ypos0)) / 2),
//             0.0f);

//         RectNode* rect_node = PushStruct(ctx->arena_frame, RectNode);
//         SLLStackPush(rect_list->first, rect_node);
//         Rect* rect = &rect_node->rect;

//         rect->p0 = {xpos0, ypos0};
//         rect->p1 = {xpos1, ypos1};
//         rect->glyph_atlas_offset = {(F32)ch->glyph_atlas_x_offset + p0_x_offset, p0_y_offset};

//         xOrigin += (F32)(ch->advance >> 6);
//     }
// }

// static Buffer<Character>
// CharacterHashmapCreate(Arena* arena, U32* width, U32* height, U32 hashmap_size,
//                        String8 font_ttf_path, U32 font_size)
// {
//     Buffer<CharacterNodeList> hashmap = BufferAlloc<CharacterNodeList>(arena, hashmap_size);
//     FT_Library ft;
//     FT_Face face;
//     if (FT_Init_FreeType(&ft))
//     {
//         exitWithError("failed to init freetype library!");
//     }

//     int error;
//     error = FT_New_Face(ft, "fonts/Roboto-Black.ttf", 0, &face);
//     if (error == FT_Err_Unknown_File_Format)
//     {
//         exitWithError("failed to load font as the file format is unknown!");
//     }
//     else if (error)
//     {
//         exitWithError("failed to load font file!");
//     }

//     FT_Set_Pixel_Sizes(face, 0, font_size);

//     *width = 0;
//     *height = 0;

//     FT_ULong charcode;
//     FT_UInt gindex;
//     charcode = FT_Get_First_Char(face, &gindex);
//     while (gindex != 0)
//     {
//         charcode = FT_Get_Next_Char(face, charcode, &gindex);
//     }
//     for (U32 i = 0; i < hashmap.capacity; i++)
//     {
//         if (FT_Load_Char(face, i, FT_LOAD_DEFAULT))
//         {
//             printf("Failed to load glyph for character %lu", i);
//             continue; // Skip this character and continue with the next
//         }

//         *width += face->glyph->bitmap.width;
//         *height = Max(*height, face->glyph->bitmap.rows);

//         charArr[i].width = (F32)(face->glyph->bitmap.width);
//         charArr[i].height = (F32)(face->glyph->bitmap.rows);
//         charArr[i].bearingX = static_cast<f32>(face->glyph->bitmap_left);
//         charArr[i].bearingY = static_cast<f32>(face->glyph->bitmap_top);
//         charArr[i].advance = static_cast<u32>(face->glyph->advance.x);
//     }
//     unsigned char* glyphBuffer;
//     glyphBuffer = PushArray(arena, u8, (*width) * (*height));
//     u32 glyphOffset = 0;
//     for (u32 i = 0; i < font->MAX_GLYPHS; i++)
//     {
//         if (FT_Load_Char(face, i, FT_LOAD_RENDER))
//         {
//             exitWithError("failed to load glyph!");
//         }
//         for (unsigned int j = 0; j < face->glyph->bitmap.rows; j++)
//         {
//             for (unsigned int k = 0; k < face->glyph->bitmap.width; k++)
//             {
//                 glyphBuffer[j * (*width) + k + glyphOffset] =
//                     face->glyph->bitmap.buffer[k + j * face->glyph->bitmap.width];
//             }
//         }
//         hashmap[i].glyphOffset = glyphOffset;
//         glyphOffset += face->glyph->bitmap.width;
//     }

//     FT_Done_Face(face);
//     FT_Done_FreeType(ft);
//     return glyphBuffer;
// }

// static void
// createGlyphAtlasTextureSampler(Font* font, VkPhysicalDevice physicalDevice, VkDevice device)
// {
//     VkSamplerCreateInfo samplerInfo{};
//     samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
//     samplerInfo.magFilter = VK_FILTER_LINEAR;
//     samplerInfo.minFilter = VK_FILTER_LINEAR;
//     samplerInfo.unnormalizedCoordinates = VK_TRUE;

//     samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
//     samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
//     samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
//     samplerInfo.anisotropyEnable = VK_FALSE;

//     VkPhysicalDeviceProperties properties{};
//     vkGetPhysicalDeviceProperties(physicalDevice, &properties);
//     // anisotropic filtering is a optional feature and has to be enabled in the device features
//     samplerInfo.maxAnisotropy =
//         properties.limits
//             .maxSamplerAnisotropy; // this is the maximum amount of texel samples that can be

//     // to calculate the final color of a texture
//     samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
//     samplerInfo.compareEnable =
//         VK_FALSE; // if true texels will be compared to a value and result used in filtering
//     samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
//     // for mipmapping
//     samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
//     samplerInfo.mipLodBias = 0.0f;
//     samplerInfo.minLod = 0.0f;
//     samplerInfo.maxLod = 0.0f;

//     // The sampler is not attached to a image so it can be applied to any image
//     if (vkCreateSampler(device, &samplerInfo, nullptr, &font->textureSampler) != VK_SUCCESS)
//     {
//         exitWithError("failed to create texture sampler!");
//     }
// }
} // namespace draw
