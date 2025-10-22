
typedef U32 Matsim_TokenType;
enum
{
    Matsim_TokenType_Invalid = (1 << 0),
    Matsim_TokenType_Str = (1 << 1),
    Matsim_TokenType_Float = (1 << 2),
    Matsim_TokenType_Int = (1 << 3),
    Matsim_TokenType_Newline = (1 << 4),
    Matsim_TokenType_Comma = (1 << 5),
    Matsim_TokenType_EOF = (1 << 6),
    Matsim_TokenType_IncompleteFloat = (1 << 7),
    Matsim_TokenType_Count = (1 << 8)
};

struct Matsim_Token
{
    U32 type;
    Rng1U64 rng;
};

struct Matsim_TokenChunkNode
{
    Matsim_TokenChunkNode* next;
    Buffer<Matsim_Token> buffer;
    U32 count;
};

struct Matsim_TokenChunkList
{
    Matsim_TokenChunkNode* first;
    Matsim_TokenChunkNode* last;
    U32 chunk_count;
    U64 token_count;
};

struct Matsim_Progress
{
    U32 byte_loc;
    U64 latest_second_read;
};
// parser
struct Matsim_TokenNode
{
    Matsim_TokenNode* next;
    Matsim_Token* token;
};

static void
Matsim_ChunkListAdd(Arena* arena, Matsim_TokenChunkList* list, U64 cap, Matsim_Token token);

static Buffer<Matsim_Token>
Matsim_BufferFromChunkList(Arena* arena, Matsim_TokenChunkList* list);

static void
Matsim_TokenTypePrint(Matsim_TokenType token_type, B32 newline_after = 0);

static void
Matsim_TokenChunkListPrint(Matsim_TokenChunkList* list);

static Buffer<Matsim_Token>
Matsim_Tokenization(Arena* arena, String8 text);
