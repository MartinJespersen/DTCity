namespace draw
{
struct Rect
{
    Vec2F32 p0;
    Vec2F32 p1;
    Vec2F32 glyph_atlas_offset;
};

struct RectNode
{
    RectNode* next;
    Rect rect;
};

struct RectNodeList
{
    RectNode* first;
    RectNode* last;
};

struct Frame
{
    RectNodeList rect_list;
};

struct Text;
struct DrawCtx
{
    Arena* arena;
    Text* text;

    Arena* arena_frame;
    Frame* frame;
};

static DrawCtx*
DrawCtxGet();

} // namespace draw
