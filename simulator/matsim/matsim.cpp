static void
Matsim_TokenTypePrint(Matsim_TokenType token_type, B32 newline_after)
{
    const char* str = 0;

    if (token_type & Matsim_TokenType_Int)
        str = "Matsim_TokenType_Int";
    else if (token_type & Matsim_TokenType_Float)
        str = "Matsim_TokenType_Float";
    else if (token_type & Matsim_TokenType_Str)
        str = "Matsim_TokenType_Str";
    else if (token_type & Matsim_TokenType_Newline)
        str = "Matsim_TokenType_Newline";
    else if (token_type & Matsim_TokenType_IncompleteFloat)
        str = "Matsim_TokenType_IncompleteFloat";
    else if (token_type & Matsim_TokenType_Comma)
        str = "Matsim_TokenType_Comma";
    else if (token_type & Matsim_TokenType_EOF)
        str = "Matsim_TokenType_EOF";
    else
    {
        Trap();
    }

    printf("%s", str);
    printf(", ");
    if (newline_after)
        printf("\n");
}

static void
Matsim_TokenChunkListPrint(Matsim_TokenChunkList* list)
{
    for (Matsim_TokenChunkNode* chunk = list->first; chunk != NULL; chunk = chunk->next)
    {
        for (U32 i = 0; i < chunk->buffer.size; ++i)
        {
            Matsim_TokenType token_type = (Matsim_TokenType)chunk->buffer.data[i].type;
            Matsim_TokenTypePrint(token_type, TRUE);
        }
    }
}

static Buffer<Matsim_Token>
Matsim_Tokenization(Arena* arena, String8 text)
{
    // ~mgj: Lexing file
    Buffer<Matsim_Token> token_buffer = {};

    Matsim_TokenChunkList list = {};
    U8* buf_past_end = text.str + text.size;
    U8* buf_start = text.str;
    U8* tok_from = buf_start;
    U8* tok_to = tok_from;
    for (; tok_to <= buf_past_end;)
    {
        Matsim_Token token = {};

        while (*tok_to == ' ' || *tok_to == '\t')
        {
            tok_to += 1;
            tok_from += 1;
        }

        if (token.type == 0 && *tok_to == 0)
        {
            token.type = Matsim_TokenType_EOF;
            tok_to += 1;
        }

        if (token.type == 0 && *tok_to == ',')
        {
            token.type = Matsim_TokenType_Comma;
            tok_to += 1;
        }

        if (token.type == 0 && *tok_to == '\n')
        {
            token.type = Matsim_TokenType_Newline;
            tok_to += 1;
        }

        // parse integer
        if (token.type == 0 && *tok_to >= '0' && *tok_to <= '9')
        {
            token.type = Matsim_TokenType_Int;
            do
            {
                tok_to += 1;
            } while (*tok_to >= '0' && *tok_to <= '9');
        }

        // parse float
        if (token.type == Matsim_TokenType_Int && *tok_to == '.')
        {
            token.type = Matsim_TokenType_Float;
            tok_to += 1;

            U8* tok_prev = tok_to;
            while (*tok_to >= '0' && *tok_to <= '9')
            {
                tok_to += 1;
            };
            if (tok_to == tok_prev)
            {
                token.type |= Matsim_TokenType_IncompleteFloat;
            }
        }

        // parse string
        U32 number_group = Matsim_TokenType_Int | Matsim_TokenType_Float;
        U32 str_qualifier = token.type & number_group;

        if ((token.type == 0 || str_qualifier != 0) &&
            (((*tok_to >= 'A' && *tok_to <= 'Z')) || ((*tok_to >= 'a' && *tok_to <= 'z')) ||
             (*tok_to == '_') || (*tok_to == '-')))
        {
            token.type = Matsim_TokenType_Str;
            do
            {
                tok_to += 1;
            } while ((*tok_to >= 'A' && *tok_to <= 'Z') || (*tok_to >= 'a' && *tok_to <= 'z') ||
                     (*tok_to >= '0' && *tok_to <= '9') || (*tok_to == '_') || (*tok_to == '-') ||
                     (*tok_to == '.') || (*tok_to == ' '));
        }

        token.rng = {(U64)(tok_from - buf_start), (U64)(tok_to - buf_start)};
        Assert(token.type);
        if (token.type)
        {
            Matsim_ChunkListAdd(arena, &list, 4096, token);
        }
        tok_from = tok_to;
    }

    token_buffer = Matsim_BufferFromChunkList(arena, &list);
    return token_buffer;
}

SIM_ParseResult
SIM_Parse(Arena* arena, String8 text)
{
    ScratchScope scratch = ScratchScope(&arena, 1);
    // ~mgj: tokenization
    Buffer<Matsim_Token> tokens = Matsim_Tokenization(scratch.arena, text);
    //////////////////////////////////////////////////////////////////
    // ~mgj: Parsing file
    auto Matsim_ParseErrorPush =
        [=](Arena* arena, SIM_ParseErrorList* error_list, U64 line_number, const char* msg, ...)
    {
        String8List list = {};

        Str8ListPush(arena, &list, PushStr8F(arena, "Error at line: %llu", line_number));

        String8 result = {0};
        va_list args;
        va_start(args, msg);
        Str8ListPush(arena, &list, PushStr8FV(arena, msg, args));
        va_end(args);

        StringJoin sep = {.sep = S(" ")};
        String8 error_msg = Str8ListJoin(arena, &list, &sep);
        SIM_ParseErrorNode* err_node = PushStruct(arena, SIM_ParseErrorNode);
        SLLQueuePush(error_list->first, error_list->last, err_node);
        err_node->message = error_msg;
    };

    // ~mgj: Parse header
    Matsim_Token* start_token = tokens.data;
    Matsim_Token* token = start_token;
    SIM_ParseErrorList error_list = {};
    SIM_AgentSnapshotChunkList agent_snapshot_list = {};
    U32 line_number = 1;
    B32 error = FALSE;
    U64 cur_timestep = 0;
    SIM_AgentSnapshot agent_snapshot = {};
    B32 end_of_file = false;

    // parse header
    Matsim_Token* header_start = token;
    U32 field_count = 0;
    do
    {
        field_count += 1;
        if (header_start != token)
        {
            if ((token->type & Matsim_TokenType_Comma) == 0)
            {
                U32 column_number = token - header_start + 1;
                Matsim_ParseErrorPush(arena, &error_list, line_number,
                                      "Expected comma before column %u", column_number);
                goto error_jump;
            }
            else
            {
                token += 1;
            }
        }

        if ((token->type & Matsim_TokenType_Str) == 0)
        {
            Matsim_ParseErrorPush(arena, &error_list, line_number, "Header parsing error column");
            goto error_jump;
        }
        else
        {
            token += 1;
        }

    } while ((token - start_token) < (S64)tokens.size && !(token->type & Matsim_TokenType_Newline));
    Assert(field_count == 4 && "Fields must be exactly 4");

    // parse body
    token += 1;
    for (; (token - start_token) < (S64)tokens.size && error == false || end_of_file == false;)
    {
        agent_snapshot = {};
        line_number += 1;

        // parse time step
        {
            if (token->type != Matsim_TokenType_Float)
            {
                Matsim_ParseErrorPush(arena, &error_list, line_number,
                                      "Expected float value for time step value");
                goto error_jump;
            }
            String8 str = Str8Substr(text, token->rng);
            F64 v = F64FromStr8(str);
            if (!v)
            {
                Matsim_ParseErrorPush(arena, &error_list, line_number,
                                      "Error parsing time step on line %u");
                goto error_jump;
            }
            agent_snapshot.timestamp = (U64)v;
            if (agent_snapshot.timestamp < cur_timestep)
            {
                Matsim_ParseErrorPush(arena, &error_list, line_number,
                                      "Time step must be monotonically increasing on line %u");
                goto error_jump;
            }
            token += 1;
        }

        // sep
        if ((token->type & Matsim_TokenType_Comma) == false)
        {
            Matsim_ParseErrorPush(arena, &error_list, line_number,
                                  "Missing seperator after timestep");
            goto error_jump;
        }
        token += 1;

        // parse id
        {
            if ((token->type & Matsim_TokenType_Str) == false)
            {
                Matsim_ParseErrorPush(arena, &error_list, line_number,
                                      "Expected float value for time step value", line_number);
                goto error_jump;
            }
            String8 str = Str8Substr(text, token->rng);
            agent_snapshot.id = PushStr8Copy(arena, str);
            token += 1;
        }

        // sep
        if ((token->type & Matsim_TokenType_Comma) == false)
        {
            Matsim_ParseErrorPush(arena, &error_list, line_number, "Missing seperator after id",
                                  line_number);
            goto error_jump;
        }
        token += 1;

        // parse location
        {
            const U32 location_dim = 2;
            const char* location[location_dim] = {"x", "y"};
            F64* location_values[location_dim] = {&agent_snapshot.x, &agent_snapshot.y};
            for (U32 i = 0; i < location_dim && error == false; i++)
            {
                // sep
                if (i != 0)
                {
                    if ((token->type & Matsim_TokenType_Comma) == false)
                    {
                        Matsim_ParseErrorPush(arena, &error_list, line_number,
                                              "Missing seperator after id", line_number);
                        goto error_jump;
                    }
                    token += 1;
                }

                if ((token->type & Matsim_TokenType_Float) == false)
                {
                    Matsim_ParseErrorPush(arena, &error_list, line_number,
                                          "Expected float value for %s location", location[i]);
                    goto error_jump;
                }

                {
                    String8 str = Str8Substr(text, token->rng);
                    F64 v = F64FromStr8(str);
                    if (!v)
                    {
                        Matsim_ParseErrorPush(arena, &error_list, line_number,
                                              "Error parsing location %s for agent %s", location[i],
                                              agent_snapshot.id);
                        goto error_jump;
                    }
                    *location_values[i] = v;
                }
                token += 1;
            }
        }

        end_of_file = false;
        if (token->type & Matsim_TokenType_EOF)
        {
            end_of_file = true;
        }

        // parse newline
        if ((token->type & Matsim_TokenType_Newline) == false && end_of_file == false)
        {
            Matsim_ParseErrorPush(arena, &error_list, line_number,
                                  "Expected newline after location values for agent %s",
                                  agent_snapshot.id);
            goto error_jump;
        }
        token += 1;
        SIM_AgentSnapshotChunkAdd(arena, &agent_snapshot_list, &agent_snapshot);
        continue;
    error_jump:
        error = true;
    }

    SIM_ParseResult result = {
        .agent_snapshot_list = agent_snapshot_list, .errors = error_list, .success = !error};
    return result;
}

static Buffer<Matsim_Token>
Matsim_BufferFromChunkList(Arena* arena, Matsim_TokenChunkList* list)
{
    Buffer<Matsim_Token> buffer = BufferAlloc<Matsim_Token>(arena, list->token_count);
    U64 offset = 0;
    for (Matsim_TokenChunkNode* chunk = list->first; chunk != 0; chunk = chunk->next)
    {
        U64 chunk_size = chunk->count;
        MemoryCopy(buffer.data + offset, chunk->buffer.data, chunk_size * sizeof(Matsim_Token));
        offset += chunk_size;
    }

    return buffer;
}

static void
Matsim_ChunkListAdd(Arena* arena, Matsim_TokenChunkList* list, U64 cap, Matsim_Token token)
{
    Matsim_TokenChunkNode* chunk = list->last;
    if (chunk == 0 || chunk->count >= cap)
    {
        chunk = PushStruct(arena, Matsim_TokenChunkNode);
        SLLQueuePush(list->first, list->last, chunk);
        chunk->buffer = BufferAlloc<Matsim_Token>(arena, cap);
        list->chunk_count += 1;
    }
    chunk->buffer.data[chunk->count++] = token;
    list->token_count += 1;
}
