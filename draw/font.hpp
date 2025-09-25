namespace draw
{

struct Character
{
    float width; // Size of glyph
    float height;
    float bearing_x; // Offset from baseline to left/top of glyph
    float bearing_y;
    unsigned int advance; // Offset to advance to next glyph
    U32 glyph_atlas_x_offset;
    U8 character;
};

struct CharacterNode
{
    CharacterNode* next;
    Character character;
};

struct CharacterNodeList
{
    CharacterNode* first;
    CharacterNode* last;
};

struct Text
{
    Buffer<CharacterNodeList> character_hashmap;
    U32 max_glyphs;
};

} // namespace draw
