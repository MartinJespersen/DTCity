#pragma once

#include "base_math.h"

// Copyright (c) 2024 Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef BASE_STRINGS_H
#define BASE_STRINGS_H

////////////////////////////////
//~ rjf: Third Party Includes
#include "third_party/stb_sprintf.h"
////////////////////////////////
// Cpp string
struct Str8
{
    U8* str;
    U64 size;

    Str8() noexcept = default;
    Str8(U64 size) noexcept;
    ~Str8() noexcept;
};

//~ rjf: String Types
struct String8
{
    U8* str;
    U64 size;
};

typedef struct String16 String16;
struct String16
{
    U16* str;
    U64 size;
};

typedef struct String32 String32;
struct String32
{
    U32* str;
    U64 size;
};

////////////////////////////////
//~ rjf: String List & Array Types

typedef struct String8Node String8Node;
struct String8Node
{
    String8Node* next;
    String8 string;
};

typedef struct String8MetaNode String8MetaNode;
struct String8MetaNode
{
    String8MetaNode* next;
    String8Node* node;
};

typedef struct String8List String8List;
struct String8List
{
    String8Node* first;
    String8Node* last;
    U64 node_count;
    U64 total_size;
};

typedef struct String8Array String8Array;
struct String8Array
{
    String8* v;
    U64 count;
};

////////////////////////////////
//~ rjf: String Matching, Splitting, & Joining Types

typedef U32 StringMatchFlags;
enum
{
    MatchFlag_CaseInsensitive = (1 << 0),
    MatchFlag_RightSideSloppy = (1 << 1),
    MatchFlag_SlashInsensitive = (1 << 2),
};

typedef U32 StringSplitFlags;
enum
{
    StringSplitFlag_KeepEmpties = (1 << 0),
};

typedef enum PathStyle
{
    PathStyle_Null,
    PathStyle_Relative,
    PathStyle_WindowsAbsolute,
    PathStyle_UnixAbsolute,

#if OS_WINDOWS
    PathStyle_SystemAbsolute = PathStyle_WindowsAbsolute
#elif OS_LINUX
    PathStyle_SystemAbsolute = PathStyle_UnixAbsolute
#else
#error "absolute path style is undefined for this OS"
#endif
} PathStyle;

typedef struct StringJoin StringJoin;
struct StringJoin
{
    String8 pre;
    String8 sep;
    String8 post;
};

////////////////////////////////
//~ rjf: String Pair Types

typedef struct String8TxtPtPair String8TxtPtPair;
struct String8TxtPtPair
{
    String8 string;
    TxtPt pt;
};

////////////////////////////////
//~ rjf: UTF Decoding Types

typedef struct UnicodeDecode UnicodeDecode;
struct UnicodeDecode
{
    U32 inc;
    U32 codepoint;
};

////////////////////////////////
//~ rjf: String Fuzzy Matching Types

typedef struct FuzzyMatchRangeNode FuzzyMatchRangeNode;
struct FuzzyMatchRangeNode
{
    FuzzyMatchRangeNode* next;
    Rng1U64 range;
};

typedef struct FuzzyMatchRangeList FuzzyMatchRangeList;
struct FuzzyMatchRangeList
{
    FuzzyMatchRangeNode* first;
    FuzzyMatchRangeNode* last;
    U64 count;
    U64 needle_part_count;
    U64 total_dim;
};

////////////////////////////////
//~ rjf: Character Classification & Conversion Functions

B32
char_is_space(U8 c);
B32
char_is_upper(U8 c);
B32
char_is_lower(U8 c);
B32
char_is_alpha(U8 c);
B32
char_is_slash(U8 c);
B32
char_is_digit(U8 c, U32 base);
U8
char_to_lower(U8 c);
U8
char_to_upper(U8 c);
U8
char_to_correct_slash(U8 c);

////////////////////////////////
//~ rjf: C-String Measurement

U64
cstring8_length(U8* c);
U64
cstring16_length(U16* c);
U64
cstring32_length(U32* c);

////////////////////////////////
//~ rjf: String Constructors

#define str8_lit(S) str8((U8*)(S), sizeof(S) - 1)
#define str8_lit_comp(S) \
    {                    \
        (U8*)(S),        \
        sizeof(S) - 1,   \
    }
#define str8_varg(S) (int)((S).size), ((S).str)

#define str8_array(S, C) str8((U8*)(S), sizeof(*(S)) * (C))
#define str8_array_fixed(S) str8((U8*)(S), sizeof(S))
#define str8_struct(S) str8((U8*)(S), sizeof(*(S)))

String8
str8(U8* str, U64 size);
String8
str8_range(U8* first, U8* one_past_last);
String8
Str8Zero();
String16
str16(U16* str, U64 size);
String16
str16_range(U16* first, U16* one_past_last);
String16
str16_zero();
String32
str32(U32* str, U64 size);
String32
str32_range(U32* first, U32* one_past_last);
String32
str32_zero();
String8
str8_c_string(const char* c);
String16
str16_cstring(U16* c);
String32
str32_cstring(U32* c);
String8
str8_cstring_capped(void* cstr, void* cap);
String16
str16_cstring_capped(void* cstr, void* cap);
String8
str8_cstring_capped_reverse(void* raw_start, void* raw_cap);

////////////////////////////////
//~ rjf: String Stylization

String8
upper_from_str8(Arena* arena, String8 string);
String8
lower_from_str8(Arena* arena, String8 string);
String8
backslashed_from_str8(Arena* arena, String8 string);

////////////////////////////////
//~ rjf: String Matching

#define str8_match_lit(a_lit, b, flags) str8_match(str8_lit(a_lit), (b), (flags))
#define str8_match_cstr(a_cstr, b, flags) str8_match(str8_c_string(a_cstr), (b), (flags))
B32
str8_match(String8 a, String8 b, StringMatchFlags flags);
U64
str8_substr_find(String8 string, String8 needle, U64 start_pos, StringMatchFlags flags);
U64
str8_find_needle_reverse(String8 string, U64 start_pos, String8 needle, StringMatchFlags flags);
B32
str8_ends_with(String8 string, String8 end, StringMatchFlags flags);
#define str8_ends_with_lit(string, end_lit, flags) str8_ends_with((string), str8_lit(end_lit), (flags))

////////////////////////////////
//~ rjf: String Slicing

String8
Str8Substr(String8 str, Rng1U64 range);
String8
str8_prefix(String8 str, U64 size);
String8
str8_skip(String8 str, U64 amt);
String8
str8_postfix(String8 str, U64 size);
String8
str8_chop(String8 str, U64 amt);
String8
str8_whitespace_skip(String8 string);
String8
str8_skip_chop_slashes(String8 string);

////////////////////////////////
//~ rjf: String Formatting & Copying

String8
str8_concat(Arena* arena, String8 s1, String8 s2);
String8
push_str8_copy(Arena* arena, String8 s);
String8
push_str8fv(Arena* arena, const char* fmt, va_list args);
String8
push_str8f(Arena* arena, const char* fmt, ...);

////////////////////////////////
//~ rjf: String <=> Integer Conversions

//- rjf: string -> integer
S64
sign_from_str8(String8 string, String8* string_tail);
B32
str8_is_integer(String8 string, U32 radix);

U64
U64FromStr8(String8 string, U32 radix);
S64
s64_from_str8(String8 string, U32 radix);
U32
u32_from_str8(String8 string, U32 radix);
S32
s32_from_str8(String8 string, U32 radix);
B32
try_u64_from_str8_c_rules(String8 string, U64* x);
B32
try_s64_from_str8_c_rules(String8 string, S64* x);

//- rjf: integer -> string
String8
str8_from_memory_size(Arena* arena, U64 size);
String8
str8_from_count(Arena* arena, U64 count);
String8
str8_from_bits_u32(Arena* arena, U32 x);
String8
str8_from_bits_u64(Arena* arena, U64 x);
String8
str8_from_u64(Arena* arena, U64 u64, U32 radix, U8 min_digits, U8 digit_group_separator);
String8
str8_from_s64(Arena* arena, S64 s64, U32 radix, U8 min_digits, U8 digit_group_separator);

////////////////////////////////
//~ rjf: String <=> Float Conversions

F64
f64_from_str8(String8 string);

////////////////////////////////
//~ rjf: String List Construction Functions

String8Node*
str8_list_push_node(String8List* list, String8Node* node);
String8Node*
str8_list_push_node_set_string(String8List* list, String8Node* node, String8 string);
String8Node*
str8_list_push_node_front(String8List* list, String8Node* node);
String8Node*
str8_list_push_node_front_set_string(String8List* list, String8Node* node, String8 string);
String8Node*
str8_list_push(Arena* arena, String8List* list, String8 string);
String8Node*
str8_list_push_front(Arena* arena, String8List* list, String8 string);
void
str8_list_concat_in_place(String8List* list, String8List* to_push);
String8Node*
str8_list_push_aligner(Arena* arena, String8List* list, U64 min, U64 align);
String8Node*
Str8ListPushF(Arena* arena, String8List* list, char* fmt, ...);
String8Node*
str8_list_push_frontf(Arena* arena, String8List* list, char* fmt, ...);
String8List
str8_list_copy(Arena* arena, String8List* list);
#define str8_list_first(list) ((list)->first ? (list)->first->string : Str8Zero())

////////////////////////////////
//~ rjf: String Splitting & Joining

String8List
str8_split(Arena* arena, String8 string, U8* split_chars, U64 split_char_count, StringSplitFlags flags);
String8List
str8_split_by_string_chars(Arena* arena, String8 string, String8 split_chars, StringSplitFlags flags);
String8List
str8_list_split_by_string_chars(Arena* arena, String8List list, String8 split_chars, StringSplitFlags flags);
String8
str8_list_join(Arena* arena, String8List* list, StringJoin* optional_params);
void
str8_list_from_flags(Arena* arena, String8List* list, U32 flags, String8* flag_string_table, U32 flag_string_count);

////////////////////////////////
//~ rjf; String Arrays

String8Array
str8_array_zero();
String8Array
str8_array_from_list(Arena* arena, String8List* list);
String8Array
str8_array_reserve(Arena* arena, U64 count);
String8Array
str8_array_copy(Arena* arena, String8Array array);

////////////////////////////////
//~ rjf: String Path Helpers

String8
str8_chop_last_slash(String8 string);
String8
str8_skip_last_slash(String8 string);
String8
str8_chop_last_dot(String8 string);
String8
str8_skip_last_dot(String8 string);

PathStyle
path_style_from_str8(String8 string);
String8List
str8_split_path(Arena* arena, String8 string);
void
str8_path_list_resolve_dots_in_place(String8List* path, PathStyle style);
String8
str8_path_list_join_by_style(Arena* arena, String8List* path, PathStyle style);

String8TxtPtPair
str8_txt_pt_pair_from_string(String8 string);

////////////////////////////////
//~ rjf: UTF-8 & UTF-16 Decoding/Encoding

UnicodeDecode
utf8_decode(U8* str, U64 max);
UnicodeDecode
utf16_decode(U16* str, U64 max);
U32
utf8_encode(U8* str, U32 codepoint);
U32
utf16_encode(U16* str, U32 codepoint);
U32
utf8_from_utf32_single(U8* buffer, U32 character);

////////////////////////////////
//~ rjf: Unicode String Conversions

String8
str8_from_16(Arena* arena, String16 in);
String16
str16_from_8(Arena* arena, String8 in);
String8
str8_from_32(Arena* arena, String32 in);
String32
Str32From8(Arena* arena, String8 in);

////////////////////////////////
//~ String -> Enum Conversions

OperatingSystem
operating_system_from_string(String8 string);

////////////////////////////////
//~ rjf: Basic Types & Space Enum -> String Conversions

String8
string_from_dimension(Dimension dimension);
String8
string_from_side(Side side);
String8
string_from_operating_system(OperatingSystem os);
String8
string_from_arch(Arch arch);

////////////////////////////////
//~ rjf: Time Types -> String

String8
string_from_week_day(WeekDay week_day);
String8
string_from_month(Month month);
String8
push_date_time_string(Arena* arena, DateTime* date_time);
String8
push_file_name_date_time_string(Arena* arena, DateTime* date_time);
String8
string_from_elapsed_time(Arena* arena, DateTime dt);

////////////////////////////////
//~ Globally Unique Ids

String8
string_from_guid(Arena* arena, Guid guid);
B32
try_guid_from_string(String8 string, Guid* guid_out);
Guid
guid_from_string(String8 string);

////////////////////////////////
//~ rjf: Basic Text Indentation

String8
indented_from_string(Arena* arena, String8 string);

////////////////////////////////
//~ rjf: Text Escaping

String8
escaped_from_raw_str8(Arena* arena, String8 string);
String8
raw_from_escaped_str8(Arena* arena, String8 string);

////////////////////////////////
//~ rjf: Text Wrapping

String8List
wrapped_lines_from_string(Arena* arena, String8 string, U64 first_line_max_width, U64 max_width, U64 wrap_indent);

////////////////////////////////
//~ rjf: String <-> Color

String8
hex_string_from_rgba_4f32(Arena* arena, Vec4F32 rgba);
Vec4F32
rgba_from_hex_string_4f32(String8 hex_string);

////////////////////////////////
//~ rjf: String Fuzzy Matching

FuzzyMatchRangeList
fuzzy_match_find(Arena* arena, String8 needle, String8 haystack);
FuzzyMatchRangeList
fuzzy_match_range_list_copy(Arena* arena, FuzzyMatchRangeList* src);

////////////////////////////////
//~ NOTE(allen): Serialization Helpers

void
str8_serial_begin(Arena* arena, String8List* srl);
String8
str8_serial_end(Arena* arena, String8List* srl);
void
str8_serial_write_to_dst(String8List* srl, void* out);
U64
str8_serial_push_align(Arena* arena, String8List* srl, U64 align);
void*
str8_serial_push_size(Arena* arena, String8List* srl, U64 size);
void*
str8_serial_push_data(Arena* arena, String8List* srl, void* data, U64 size);
void
str8_serial_push_data_list(Arena* arena, String8List* srl, String8Node* first);
void
str8_serial_push_u64(Arena* arena, String8List* srl, U64 x);
void
str8_serial_push_u32(Arena* arena, String8List* srl, U32 x);
void
str8_serial_push_u16(Arena* arena, String8List* srl, U16 x);
void
str8_serial_push_u8(Arena* arena, String8List* srl, U8 x);
void
str8_serial_push_cstr(Arena* arena, String8List* srl, String8 str);
void
str8_serial_push_string(Arena* arena, String8List* srl, String8 str);
#define str8_serial_push_array(arena, srl, ptr, count) str8_serial_push_data(arena, srl, ptr, sizeof(*(ptr)) * (count))
#define str8_serial_push_struct(arena, srl, ptr) str8_serial_push_array(arena, srl, ptr, 1)

////////////////////////////////
//~ rjf: Deserialization Helpers

U64
str8_deserial_read(String8 string, U64 off, void* read_dst, U64 read_size, U64 granularity);
U64
str8_deserial_find_first_match(String8 string, U64 off, U16 scan_val);
void*
str8_deserial_get_raw_ptr(String8 string, U64 off, U64 size);
U64
str8_deserial_read_cstr(String8 string, U64 off, String8* cstr_out);
U64
str8_deserial_read_windows_utf16_string16(String8 string, U64 off, String16* str_out);
U64
str8_deserial_read_block(String8 string, U64 off, U64 size, String8* block_out);
#define str8_deserial_read_array(string, off, ptr, count) \
    str8_deserial_read((string), (off), (ptr), sizeof(*(ptr)) * (count), sizeof(*(ptr)))
#define str8_deserial_read_struct(string, off, ptr) str8_deserial_read_array(string, off, ptr, 1)

//- rjf: Allocation
String8
push_str8_copy(Arena* arena, String8 string);
String8
PushStr8FV(Arena* arena, const char* fmt, va_list args);
String8
PushStr8F(Arena* arena, const char* fmt, ...);
String8
push_str8_fill_byte(Arena* arena, U64 size, U8 byte);

// ~mgj: String <==> float/double copy
B32
F32FromStr8(String8 string, F32* out);

// ~mgj: String hashes
U128
hash_u128_from_str8(String8 str);

// ~mgj: environment
String8List
env_vars_from_env_file(Arena* arena);
B32
env_vars_value_get(Arena* arena, const String8 key, String8* out_value, bool env_file_included);

// ~mgj: Errors
void
exit_with_error(const char* msg, ...);

// string manipulation helpers
#define c_str_equal(a, b) (!strcmp((a), (b)))
// ~mgj: String Macros
#define Str8Cmp(a, b) (!strcmp(((char*)(a).str), ((char*)(b).str)))
#define S(str) str8((U8*)(str), sizeof(str) - 1)
#endif // BASE_STRINGS_H
