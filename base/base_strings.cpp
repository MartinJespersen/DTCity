// Copyright (c) 2024 Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Third Party Includes

DISABLE_WARNINGS_PUSH
#define STB_SPRINTF_IMPLEMENTATION
#define STB_SPRINTF_STATIC
#include "third_party/stb_sprintf.h"
#include "third_party/meow_hash_x64_aesni.h"
DISABLE_WARNINGS_POP
////////////////////////////////
//~ NOTE(allen): String <-> Integer Tables

read_only static U8 integer_symbols[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
};

// NOTE(allen): Includes reverses for uppercase and lowercase hex.
read_only static U8 integer_symbol_reverse[128] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

read_only static U8 base64[64] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
    'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '_', '$',
};

read_only static U8 base64_reverse[128] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0xFF, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32,
    0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0xFF, 0xFF, 0xFF, 0xFF, 0x3E,
    0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

////////////////////////////////
//~ rjf: Character Classification & Conversion Functions

static B32
char_is_space(U8 c)
{
    return (c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\f' || c == '\v');
}

static B32
char_is_upper(U8 c)
{
    return ('A' <= c && c <= 'Z');
}

static B32
char_is_lower(U8 c)
{
    return ('a' <= c && c <= 'z');
}

static B32
char_is_alpha(U8 c)
{
    return (char_is_upper(c) || char_is_lower(c));
}

static B32
char_is_slash(U8 c)
{
    return (c == '/' || c == '\\');
}

static B32
char_is_digit(U8 c, U32 base)
{
    B32 result = 0;
    if (0 < base && base <= 16)
    {
        U8 val = integer_symbol_reverse[c];
        if (val < base)
        {
            result = 1;
        }
    }
    return (result);
}

static U8
char_to_lower(U8 c)
{
    if (char_is_upper(c))
    {
        c += ('a' - 'A');
    }
    return (c);
}

static U8
char_to_upper(U8 c)
{
    if (char_is_lower(c))
    {
        c += ('A' - 'a');
    }
    return (c);
}

static U8
char_to_correct_slash(U8 c)
{
    if (char_is_slash(c))
    {
        c = '/';
    }
    return (c);
}

////////////////////////////////
//~ rjf: C-String Measurement

static U64
cstring8_length(U8* c)
{
    U8* p = c;
    for (; *p != 0; p += 1)
        ;
    return (p - c);
}

static U64
cstring16_length(U16* c)
{
    U16* p = c;
    for (; *p != 0; p += 1)
        ;
    return (p - c);
}

static U64
cstring32_length(U32* c)
{
    U32* p = c;
    for (; *p != 0; p += 1)
        ;
    return (p - c);
}

////////////////////////////////
//~ rjf: String Constructors

static String8
Str8(U8* str, U64 size)
{
    String8 result = {str, size};
    return (result);
}

static String8
str8_range(U8* first, U8* one_past_last)
{
    String8 result = {first, (U64)(one_past_last - first)};
    return (result);
}

static String8
Str8Zero(void)
{
    String8 result = {0};
    return (result);
}

static String16
str16(U16* str, U64 size)
{
    String16 result = {str, size};
    return (result);
}

static String16
str16_range(U16* first, U16* one_past_last)
{
    String16 result = {first, (U64)(one_past_last - first)};
    return (result);
}

static String16
str16_zero(void)
{
    String16 result = {0};
    return (result);
}

static String32
str32(U32* str, U64 size)
{
    String32 result = {str, size};
    return (result);
}

static String32
str32_range(U32* first, U32* one_past_last)
{
    String32 result = {first, (U64)(one_past_last - first)};
    return (result);
}

static String32
str32_zero(void)
{
    String32 result = {0};
    return (result);
}

static String8
Str8CString(const char* c)
{
    String8 result = {(U8*)c, cstring8_length((U8*)c)};
    return (result);
}

static String16
str16_cstring(U16* c)
{
    String16 result = {(U16*)c, cstring16_length((U16*)c)};
    return (result);
}

static String32
str32_cstring(U32* c)
{
    String32 result = {(U32*)c, cstring32_length((U32*)c)};
    return (result);
}

static String8
str8_cstring_capped(void* cstr, void* cap)
{
    char* ptr = (char*)cstr;
    char* opl = (char*)cap;
    for (; ptr < opl && *ptr != 0; ptr += 1)
        ;
    U64 size = (U64)(ptr - (char*)cstr);
    String8 result = Str8((U8*)cstr, size);
    return result;
}

static String16
str16_cstring_capped(void* cstr, void* cap)
{
    U16* ptr = (U16*)cstr;
    U16* opl = (U16*)cap;
    for (; ptr < opl && *ptr != 0; ptr += 1)
        ;
    U64 size = (U64)(ptr - (U16*)cstr);
    String16 result = str16((U16*)cstr, size);
    return result;
}

static String8
str8_cstring_capped_reverse(void* raw_start, void* raw_cap)
{
    U8* start = (U8*)raw_start;
    U8* ptr = (U8*)raw_cap;
    for (; ptr > start;)
    {
        ptr -= 1;

        if (*ptr == '\0')
        {
            break;
        }
    }
    U64 size = (U64)(ptr - start);
    String8 result = Str8(start, size);
    return result;
}

////////////////////////////////
//~ rjf: String Stylization

static String8
upper_from_str8(Arena* arena, String8 string)
{
    string = PushStr8Copy(arena, string);
    for (U64 idx = 0; idx < string.size; idx += 1)
    {
        string.str[idx] = char_to_upper(string.str[idx]);
    }
    return string;
}

static String8
lower_from_str8(Arena* arena, String8 string)
{
    string = PushStr8Copy(arena, string);
    for (U64 idx = 0; idx < string.size; idx += 1)
    {
        string.str[idx] = char_to_lower(string.str[idx]);
    }
    return string;
}

static String8
backslashed_from_str8(Arena* arena, String8 string)
{
    string = PushStr8Copy(arena, string);
    for (U64 idx = 0; idx < string.size; idx += 1)
    {
        string.str[idx] = char_is_slash(string.str[idx]) ? '\\' : string.str[idx];
    }
    return string;
}

////////////////////////////////
//~ rjf: String Matching

static B32
Str8Match(String8 a, String8 b, StringMatchFlags flags)
{
    B32 result = 0;
    if (a.size == b.size && flags == 0)
    {
        result = MemoryMatch(a.str, b.str, b.size);
    }
    else if (a.size == b.size || (flags & MatchFlag_RightSideSloppy))
    {
        B32 case_insensitive = (flags & MatchFlag_RightSideSloppy);
        B32 slash_insensitive = (flags & MatchFlag_RightSideSloppy);
        U64 size = Min(a.size, b.size);
        result = 1;
        for (U64 i = 0; i < size; i += 1)
        {
            U8 at = a.str[i];
            U8 bt = b.str[i];
            if (case_insensitive)
            {
                at = char_to_upper(at);
                bt = char_to_upper(bt);
            }
            if (slash_insensitive)
            {
                at = char_to_correct_slash(at);
                bt = char_to_correct_slash(bt);
            }
            if (at != bt)
            {
                result = 0;
                break;
            }
        }
    }
    return result;
}

static U64
FindSubstr8(String8 string, String8 needle, U64 start_pos, StringMatchFlags flags)
{
    U8* p = string.str + start_pos;
    U64 stop_offset = Max(string.size + 1, needle.size) - needle.size;
    U8* stop_p = string.str + stop_offset;
    if (needle.size > 0)
    {
        U8* string_opl = string.str + string.size;
        String8 needle_tail = Str8Skip(needle, 1);
        StringMatchFlags adjusted_flags = flags | MatchFlag_RightSideSloppy;
        U8 needle_first_char_adjusted = needle.str[0];
        if (adjusted_flags & MatchFlag_RightSideSloppy)
        {
            needle_first_char_adjusted = char_to_upper(needle_first_char_adjusted);
        }
        for (; p < stop_p; p += 1)
        {
            U8 haystack_char_adjusted = *p;
            if (adjusted_flags & MatchFlag_CaseInsensitive)
            {
                haystack_char_adjusted = char_to_upper(haystack_char_adjusted);
            }
            if (haystack_char_adjusted == needle_first_char_adjusted)
            {
                if (Str8Match(str8_range(p + 1, string_opl), needle_tail, adjusted_flags))
                {
                    break;
                }
            }
        }
    }
    U64 result = string.size;
    if (p < stop_p)
    {
        result = (U64)(p - string.str);
    }
    return (result);
}

static U64
str8_find_needle_reverse(String8 string, U64 start_pos, String8 needle, StringMatchFlags flags)
{
    U64 result = 0;
    for (S64 i = string.size - start_pos - needle.size; i >= 0; --i)
    {
        String8 haystack = str8_substr(string, rng_1u64(i, i + needle.size));
        if (Str8Match(haystack, needle, flags))
        {
            result = (U64)i + needle.size;
            break;
        }
    }
    return result;
}

static B32
str8_ends_with(String8 string, String8 end, StringMatchFlags flags)
{
    String8 postfix = str8_postfix(string, end.size);
    B32 is_match = Str8Match(end, postfix, flags);
    return is_match;
}

////////////////////////////////
//~ rjf: String Slicing

static String8
str8_substr(String8 str, Rng1U64 range)
{
    range.min = ClampTop(range.min, str.size);
    range.max = ClampTop(range.max, str.size);
    str.str += range.min;
    str.size = dim_1u64(range);
    return (str);
}

static String8
Str8Prefix(String8 str, U64 size)
{
    str.size = ClampTop(size, str.size);
    return (str);
}

static String8
Str8Skip(String8 str, U64 amt)
{
    amt = ClampTop(amt, str.size);
    str.str += amt;
    str.size -= amt;
    return (str);
}

static String8
str8_postfix(String8 str, U64 size)
{
    size = ClampTop(size, str.size);
    str.str = (str.str + str.size) - size;
    str.size = size;
    return (str);
}

static String8
str8_chop(String8 str, U64 amt)
{
    amt = ClampTop(amt, str.size);
    str.size -= amt;
    return (str);
}

static String8
str8_skip_chop_whitespace(String8 string)
{
    U8* first = string.str;
    U8* opl = first + string.size;
    for (; first < opl; first += 1)
    {
        if (!char_is_space(*first))
        {
            break;
        }
    }
    for (; opl > first;)
    {
        opl -= 1;
        if (!char_is_space(*opl))
        {
            opl += 1;
            break;
        }
    }
    String8 result = str8_range(first, opl);
    return result;
}

static String8
str8_skip_chop_slashes(String8 string)
{
    U8* first = string.str;
    U8* opl = first + string.size;
    for (; first < opl; first += 1)
    {
        if (!char_is_slash(*first))
        {
            break;
        }
    }
    for (; opl > first;)
    {
        opl -= 1;
        if (!char_is_slash(*opl))
        {
            opl += 1;
            break;
        }
    }
    String8 result = str8_range(first, opl);
    return result;
}

////////////////////////////////
//~ rjf: String Formatting & Copying

static String8
PushStr8Cat(Arena* arena, String8 s1, String8 s2)
{
    String8 str;
    str.size = s1.size + s2.size;
    str.str = PushArrayNoZero(arena, U8, str.size + 1);
    MemoryCopy(str.str, s1.str, s1.size);
    MemoryCopy(str.str + s1.size, s2.str, s2.size);
    str.str[str.size] = 0;
    return (str);
}

static String8
push_str8fv(Arena* arena, const char* fmt, va_list args)
{
    va_list args2;
    va_copy(args2, args);
    U32 needed_bytes = vsnprintf(0, 0, fmt, args) + 1;
    String8 result = {0};
    result.str = PushArrayNoZero(arena, U8, needed_bytes);
    result.size = vsnprintf((char*)result.str, needed_bytes, fmt, args2);
    result.str[result.size] = 0;
    va_end(args2);
    return (result);
}

static String8
push_str8f(Arena* arena, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    String8 result = push_str8fv(arena, fmt, args);
    va_end(args);
    return (result);
}

////////////////////////////////
//~ rjf: String <=> Integer Conversions

//- rjf: string -> integer

static S64
sign_from_str8(String8 string, String8* string_tail)
{
    // count negative signs
    U64 neg_count = 0;
    U64 i = 0;
    for (; i < string.size; i += 1)
    {
        if (string.str[i] == '-')
        {
            neg_count += 1;
        }
        else if (string.str[i] != '+')
        {
            break;
        }
    }

    // output part of string after signs
    *string_tail = Str8Skip(string, i);

    // output integer sign
    S64 sign = (neg_count & 1) ? -1 : +1;
    return (sign);
}

static B32
str8_is_integer(String8 string, U32 radix)
{
    B32 result = 0;
    if (string.size > 0)
    {
        if (1 < radix && radix <= 16)
        {
            result = 1;
            for (U64 i = 0; i < string.size; i += 1)
            {
                U8 c = string.str[i];
                if (!(c < 0x80) || integer_symbol_reverse[c] >= radix)
                {
                    result = 0;
                    break;
                }
            }
        }
    }
    return (result);
}

static U64
U64FromStr8(String8 string, U32 radix)
{
    U64 x = 0;
    if (1 < radix && radix <= 16)
    {
        for (U64 i = 0; i < string.size; i += 1)
        {
            x *= radix;
            x += integer_symbol_reverse[string.str[i] & 0x7F];
        }
    }
    return (x);
}

static S64
s64_from_str8(String8 string, U32 radix)
{
    S64 sign = sign_from_str8(string, &string);
    S64 x = (S64)U64FromStr8(string, radix) * sign;
    return (x);
}

static U32
u32_from_str8(String8 string, U32 radix)
{
    U64 x64 = U64FromStr8(string, radix);
    U32 x32 = safe_cast_u32(x64);
    return x32;
}

static S32
s32_from_str8(String8 string, U32 radix)
{
    S64 x64 = s64_from_str8(string, radix);
    S32 x32 = safe_cast_s32(x64);
    return x32;
}

static B32
try_u64_from_str8_c_rules(String8 string, U64* x)
{
    B32 is_integer = 0;
    if (str8_is_integer(string, 10))
    {
        is_integer = 1;
        *x = U64FromStr8(string, 10);
    }
    else
    {
        String8 hex_string = Str8Skip(string, 2);
        if (Str8Match(Str8Prefix(string, 2), Str8Lit("0x"), 0) && str8_is_integer(hex_string, 0x10))
        {
            is_integer = 1;
            *x = U64FromStr8(hex_string, 0x10);
        }
        else if (Str8Match(Str8Prefix(string, 2), Str8Lit("0b"), 0) &&
                 str8_is_integer(hex_string, 2))
        {
            is_integer = 1;
            *x = U64FromStr8(hex_string, 2);
        }
        else
        {
            String8 oct_string = Str8Skip(string, 1);
            if (Str8Match(Str8Prefix(string, 1), Str8Lit("0"), 0) &&
                str8_is_integer(hex_string, 010))
            {
                is_integer = 1;
                *x = U64FromStr8(oct_string, 010);
            }
        }
    }
    return (is_integer);
}

static B32
try_s64_from_str8_c_rules(String8 string, S64* x)
{
    String8 string_tail = {0};
    S64 sign = sign_from_str8(string, &string_tail);
    U64 x_u64 = 0;
    B32 is_integer = try_u64_from_str8_c_rules(string_tail, &x_u64);
    *x = x_u64 * sign;
    return (is_integer);
}

//- rjf: integer -> string

static String8
str8_from_memory_size(Arena* arena, U64 size)
{
    String8 result;

    if (size < KB(1))
    {
        result = push_str8f(arena, "%llu Bytes", size);
    }
    else if (size < MB(1))
    {
        result = push_str8f(arena, "%llu.%02llu KiB", size / KB(1), ((size * 100) / KB(1)) % 100);
    }
    else if (size < GB(1))
    {
        result = push_str8f(arena, "%llu.%02llu MiB", size / MB(1), ((size * 100) / MB(1)) % 100);
    }
    else if (size < TB(1))
    {
        result = push_str8f(arena, "%llu.%02llu GiB", size / GB(1), ((size * 100) / GB(1)) % 100);
    }
    else
    {
        result = push_str8f(arena, "%llu.%02llu TiB", size / TB(1), ((size * 100) / TB(1)) % 100);
    }

    return result;
}

static String8
str8_from_count(Arena* arena, U64 count)
{
    String8 result;

    if (count < 1 * 1000)
    {
        result = push_str8f(arena, "%llu", count);
    }
    else if (count < 1000000)
    {
        U64 frac = ((count * 100) / 1000) % 100;
        if (frac > 0)
        {
            result = push_str8f(arena, "%llu.%02lluK", count / 1000, frac);
        }
        else
        {
            result = push_str8f(arena, "%lluK", count / 1000);
        }
    }
    else if (count < 1000000000)
    {
        U64 frac = ((count * 100) / 1000000) % 100;
        if (frac > 0)
        {
            result = push_str8f(arena, "%llu.%02lluM", count / 1000000, frac);
        }
        else
        {
            result = push_str8f(arena, "%lluM", count / 1000000);
        }
    }
    else
    {
        U64 frac = ((count * 100) * 1000000000) % 100;
        if (frac > 0)
        {
            result = push_str8f(arena, "%llu.%02lluB", count / 1000000000, frac);
        }
        else
        {
            result = push_str8f(arena, "%lluB", count / 1000000000, frac);
        }
    }

    return result;
}

static String8
str8_from_bits_u32(Arena* arena, U32 x)
{
    U8 c0 = 'a' + ((x >> 28) & 0xf);
    U8 c1 = 'a' + ((x >> 24) & 0xf);
    U8 c2 = 'a' + ((x >> 20) & 0xf);
    U8 c3 = 'a' + ((x >> 16) & 0xf);
    U8 c4 = 'a' + ((x >> 12) & 0xf);
    U8 c5 = 'a' + ((x >> 8) & 0xf);
    U8 c6 = 'a' + ((x >> 4) & 0xf);
    U8 c7 = 'a' + ((x >> 0) & 0xf);
    String8 result = push_str8f(arena, "%c%c%c%c%c%c%c%c", c0, c1, c2, c3, c4, c5, c6, c7);
    return result;
}

static String8
str8_from_bits_u64(Arena* arena, U64 x)
{
    U8 c0 = 'a' + ((x >> 60) & 0xf);
    U8 c1 = 'a' + ((x >> 56) & 0xf);
    U8 c2 = 'a' + ((x >> 52) & 0xf);
    U8 c3 = 'a' + ((x >> 48) & 0xf);
    U8 c4 = 'a' + ((x >> 44) & 0xf);
    U8 c5 = 'a' + ((x >> 40) & 0xf);
    U8 c6 = 'a' + ((x >> 36) & 0xf);
    U8 c7 = 'a' + ((x >> 32) & 0xf);
    U8 c8 = 'a' + ((x >> 28) & 0xf);
    U8 c9 = 'a' + ((x >> 24) & 0xf);
    U8 ca = 'a' + ((x >> 20) & 0xf);
    U8 cb = 'a' + ((x >> 16) & 0xf);
    U8 cc = 'a' + ((x >> 12) & 0xf);
    U8 cd = 'a' + ((x >> 8) & 0xf);
    U8 ce = 'a' + ((x >> 4) & 0xf);
    U8 cf = 'a' + ((x >> 0) & 0xf);
    String8 result = push_str8f(arena, "%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c", c0, c1, c2, c3, c4, c5,
                                c6, c7, c8, c9, ca, cb, cc, cd, ce, cf);
    return result;
}

static String8
str8_from_u64(Arena* arena, U64 u64, U32 radix, U8 min_digits, U8 digit_group_separator)
{
    String8 result = {0};
    {
        // rjf: prefix
        String8 prefix = {0};
        switch (radix)
        {
        case 16:
        {
            prefix = Str8Lit("0x");
        }
        break;
        case 8:
        {
            prefix = Str8Lit("0o");
        }
        break;
        case 2:
        {
            prefix = Str8Lit("0b");
        }
        break;
        }

        // rjf: determine # of chars between separators
        U8 digit_group_size = 3;
        switch (radix)
        {
        default:
            break;
        case 2:
        case 8:
        case 16:
        {
            digit_group_size = 4;
        }
        break;
        }

        // rjf: prep
        U64 needed_leading_0s = 0;
        {
            U64 needed_digits = 1;
            {
                U64 u64_reduce = u64;
                for (;;)
                {
                    u64_reduce /= radix;
                    if (u64_reduce == 0)
                    {
                        break;
                    }
                    needed_digits += 1;
                }
            }
            needed_leading_0s = (min_digits > needed_digits) ? min_digits - needed_digits : 0;
            U64 needed_separators = 0;
            if (digit_group_separator != 0)
            {
                needed_separators = (needed_digits + needed_leading_0s) / digit_group_size;
                if (needed_separators > 0 &&
                    (needed_digits + needed_leading_0s) % digit_group_size == 0)
                {
                    needed_separators -= 1;
                }
            }
            result.size = prefix.size + needed_leading_0s + needed_separators + needed_digits;
            result.str = PushArrayNoZero(arena, U8, result.size + 1);
            result.str[result.size] = 0;
        }

        // rjf: fill contents
        {
            U64 u64_reduce = u64;
            U64 digits_until_separator = digit_group_size;
            for (U64 idx = 0; idx < result.size; idx += 1)
            {
                if (digits_until_separator == 0 && digit_group_separator != 0)
                {
                    result.str[result.size - idx - 1] = digit_group_separator;
                    digits_until_separator = digit_group_size + 1;
                }
                else
                {
                    result.str[result.size - idx - 1] =
                        char_to_lower(integer_symbols[u64_reduce % radix]);
                    u64_reduce /= radix;
                }
                digits_until_separator -= 1;
                if (u64_reduce == 0)
                {
                    break;
                }
            }
            for (U64 leading_0_idx = 0; leading_0_idx < needed_leading_0s; leading_0_idx += 1)
            {
                result.str[prefix.size + leading_0_idx] = '0';
            }
        }

        // rjf: fill prefix
        if (prefix.size != 0)
        {
            MemoryCopy(result.str, prefix.str, prefix.size);
        }
    }
    return result;
}

static String8
str8_from_s64(Arena* arena, S64 s64, U32 radix, U8 min_digits, U8 digit_group_separator)
{
    String8 result = {0};
    // TODO(rjf): preeeeetty sloppy...
    if (s64 < 0)
    {
        Temp scratch = ScratchBegin(&arena, 1);
        String8 numeric_part =
            str8_from_u64(scratch.arena, (U64)(-s64), radix, min_digits, digit_group_separator);
        result = push_str8f(arena, "-%s", numeric_part.str);
        ScratchEnd(scratch);
    }
    else
    {
        result = str8_from_u64(arena, (U64)s64, radix, min_digits, digit_group_separator);
    }
    return result;
}

////////////////////////////////
//~ rjf: String <=> Float Conversions

static F64
f64_from_str8(String8 string)
{
    // TODO(rjf): crappy implementation for now that just uses atof.
    F64 result = 0;
    if (string.size > 0)
    {
        // rjf: find starting pos of numeric string, as well as sign
        F64 sign = +1.0;
        if (string.str[0] == '-')
        {
            sign = -1.0;
        }
        else if (string.str[0] == '+')
        {
            sign = 1.0;
        }

        // rjf: gather numerics
        U64 num_valid_chars = 0;
        char buffer[64];
        B32 exp = 0;
        for (U64 idx = 0; idx < string.size && num_valid_chars < sizeof(buffer) - 1; idx += 1)
        {
            if (char_is_digit(string.str[idx], 10) || string.str[idx] == '.' ||
                string.str[idx] == 'e' ||
                (exp && (string.str[idx] == '+' || string.str[idx] == '-')))
            {
                buffer[num_valid_chars] = string.str[idx];
                num_valid_chars += 1;
                exp = 0;
                exp = (string.str[idx] == 'e');
            }
        }

        // rjf: null-terminate (the reason for all of this!!!!!!)
        buffer[num_valid_chars] = 0;

        // rjf: do final conversion
        result = sign * atof(buffer);
    }
    return result;
}

////////////////////////////////
//~ rjf: String List Construction Functions

static String8Node*
str8_list_push_node(String8List* list, String8Node* node)
{
    SLLQueuePush(list->first, list->last, node);
    list->node_count += 1;
    list->total_size += node->string.size;
    return (node);
}

static String8Node*
str8_list_push_node_set_string(String8List* list, String8Node* node, String8 string)
{
    SLLQueuePush(list->first, list->last, node);
    list->node_count += 1;
    list->total_size += string.size;
    node->string = string;
    return (node);
}

static String8Node*
str8_list_push_node_front(String8List* list, String8Node* node)
{
    SLLQueuePushFront(list->first, list->last, node);
    list->node_count += 1;
    list->total_size += node->string.size;
    return (node);
}

static String8Node*
str8_list_push_node_front_set_string(String8List* list, String8Node* node, String8 string)
{
    SLLQueuePushFront(list->first, list->last, node);
    list->node_count += 1;
    list->total_size += string.size;
    node->string = string;
    return (node);
}

static String8Node*
Str8ListPush(Arena* arena, String8List* list, String8 string)
{
    String8Node* node = PushArrayNoZero(arena, String8Node, 1);
    str8_list_push_node_set_string(list, node, string);
    return (node);
}

static String8Node*
str8_list_push_front(Arena* arena, String8List* list, String8 string)
{
    String8Node* node = PushArrayNoZero(arena, String8Node, 1);
    str8_list_push_node_front_set_string(list, node, string);
    return (node);
}

static void
str8_list_concat_in_place(String8List* list, String8List* to_push)
{
    if (to_push->node_count != 0)
    {
        if (list->last)
        {
            list->node_count += to_push->node_count;
            list->total_size += to_push->total_size;
            list->last->next = to_push->first;
            list->last = to_push->last;
        }
        else
        {
            *list = *to_push;
        }
        MemoryZeroStruct(to_push);
    }
}

static String8Node*
str8_list_push_aligner(Arena* arena, String8List* list, U64 min, U64 align)
{
    String8Node* node = PushArrayNoZero(arena, String8Node, 1);
    U64 new_size = list->total_size + min;
    U64 increase_size = 0;
    if (align > 1)
    {
        // NOTE(allen): assert is power of 2
        Assert(((align - 1) & align) == 0);
        U64 mask = align - 1;
        new_size += mask;
        new_size &= (~mask);
        increase_size = new_size - list->total_size;
    }
    local_persist const U8 zeroes_buffer[64] = {0};
    Assert(increase_size <= ArrayCount(zeroes_buffer));
    SLLQueuePush(list->first, list->last, node);
    list->node_count += 1;
    list->total_size = new_size;
    node->string.str = (U8*)zeroes_buffer;
    node->string.size = increase_size;
    return (node);
}

static String8Node*
Str8ListPushF(Arena* arena, String8List* list, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    String8 string = push_str8fv(arena, fmt, args);
    String8Node* result = Str8ListPush(arena, list, string);
    va_end(args);
    return (result);
}

static String8Node*
str8_list_push_frontf(Arena* arena, String8List* list, char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    String8 string = push_str8fv(arena, fmt, args);
    String8Node* result = str8_list_push_front(arena, list, string);
    va_end(args);
    return (result);
}

static String8List
str8_list_copy(Arena* arena, String8List* list)
{
    String8List result = {0};
    for (String8Node* node = list->first; node != 0; node = node->next)
    {
        String8Node* new_node = PushArrayNoZero(arena, String8Node, 1);
        String8 new_string = PushStr8Copy(arena, node->string);
        str8_list_push_node_set_string(&result, new_node, new_string);
    }
    return (result);
}

static String8List
str8_split(Arena* arena, String8 string, U8* split_chars, U64 split_char_count,
           StringSplitFlags flags)
{
    String8List list = {0};

    B32 keep_empties = (flags & StringSplitFlag_KeepEmpties);

    U8* ptr = string.str;
    U8* opl = string.str + string.size;
    for (; ptr < opl;)
    {
        U8* first = ptr;
        for (; ptr < opl; ptr += 1)
        {
            U8 c = *ptr;
            B32 is_split = 0;
            for (U64 i = 0; i < split_char_count; i += 1)
            {
                if (split_chars[i] == c)
                {
                    is_split = 1;
                    break;
                }
            }
            if (is_split)
            {
                break;
            }
        }

        String8 string_local = str8_range(first, ptr);
        if (keep_empties || string_local.size > 0)
        {
            Str8ListPush(arena, &list, string_local);
        }
        ptr += 1;
    }

    return (list);
}

static String8List
str8_split_by_string_chars(Arena* arena, String8 string, String8 split_chars,
                           StringSplitFlags flags)
{
    String8List list = str8_split(arena, string, split_chars.str, split_chars.size, flags);
    return list;
}

static String8List
str8_list_split_by_string_chars(Arena* arena, String8List list, String8 split_chars,
                                StringSplitFlags flags)
{
    String8List result = {0};
    for (String8Node* node = list.first; node != 0; node = node->next)
    {
        String8List split = str8_split_by_string_chars(arena, node->string, split_chars, flags);
        str8_list_concat_in_place(&result, &split);
    }
    return result;
}

static String8
Str8ListJoin(Arena* arena, String8List* list, StringJoin* optional_params)
{
    StringJoin join = {0};
    if (optional_params != 0)
    {
        MemoryCopyStruct(&join, optional_params);
    }

    U64 sep_count = 0;
    if (list->node_count > 0)
    {
        sep_count = list->node_count - 1;
    }

    String8 result;
    result.size = join.pre.size + join.post.size + sep_count * join.sep.size + list->total_size;
    U8* ptr = result.str = PushArrayNoZero(arena, U8, result.size + 1);

    MemoryCopy(ptr, join.pre.str, join.pre.size);
    ptr += join.pre.size;
    for (String8Node* node = list->first; node != 0; node = node->next)
    {
        MemoryCopy(ptr, node->string.str, node->string.size);
        ptr += node->string.size;
        if (node->next != 0)
        {
            MemoryCopy(ptr, join.sep.str, join.sep.size);
            ptr += join.sep.size;
        }
    }
    MemoryCopy(ptr, join.post.str, join.post.size);
    ptr += join.post.size;

    *ptr = 0;

    return (result);
}

static void
str8_list_from_flags(Arena* arena, String8List* list, U32 flags, String8* flag_string_table,
                     U32 flag_string_count)
{
    for (U32 i = 0; i < flag_string_count; i += 1)
    {
        U32 flag = (1 << i);
        if (flags & flag)
        {
            Str8ListPush(arena, list, flag_string_table[i]);
        }
    }
}

////////////////////////////////
//~ rjf; String Arrays

static String8Array
str8_array_zero(void)
{
    String8Array result = {0};
    return result;
}

static String8Array
str8_array_from_list(Arena* arena, String8List* list)
{
    String8Array array;
    array.count = list->node_count;
    array.v = PushArrayNoZero(arena, String8, array.count);
    U64 idx = 0;
    for (String8Node* n = list->first; n != 0; n = n->next, idx += 1)
    {
        array.v[idx] = n->string;
    }
    return array;
}

static String8Array
str8_array_reserve(Arena* arena, U64 count)
{
    String8Array arr;
    arr.count = 0;
    arr.v = PushArray(arena, String8, count);
    return arr;
}

static String8Array
str8_array_copy(Arena* arena, String8Array array)
{
    String8Array result = {0};
    result.count = array.count;
    result.v = PushArray(arena, String8, result.count);
  for
      EachIndex(idx, result.count)
      {
          result.v[idx] = PushStr8Copy(arena, array.v[idx]);
      }
  return result;
}

////////////////////////////////
//~ rjf: String Path Helpers

static String8
str8_chop_last_slash(String8 string)
{
    if (string.size > 0)
    {
        U8* ptr = string.str + string.size - 1;
        for (; ptr >= string.str; ptr -= 1)
        {
            if (*ptr == '/' || *ptr == '\\')
            {
                break;
            }
        }
        if (ptr >= string.str)
        {
            string.size = (U64)(ptr - string.str);
        }
        else
        {
            string.size = 0;
        }
    }
    return (string);
}

static String8
str8_skip_last_slash(String8 string)
{
    if (string.size > 0)
    {
        U8* ptr = string.str + string.size - 1;
        for (; ptr >= string.str; ptr -= 1)
        {
            if (*ptr == '/' || *ptr == '\\')
            {
                break;
            }
        }
        if (ptr >= string.str)
        {
            ptr += 1;
            string.size = (U64)(string.str + string.size - ptr);
            string.str = ptr;
        }
    }
    return (string);
}

static String8
str8_chop_last_dot(String8 string)
{
    String8 result = string;
    U64 p = string.size;
    for (; p > 0;)
    {
        p -= 1;
        if (string.str[p] == '.')
        {
            result = Str8Prefix(string, p);
            break;
        }
    }
    return (result);
}

static String8
str8_skip_last_dot(String8 string)
{
    String8 result = string;
    U64 p = string.size;
    for (; p > 0;)
    {
        p -= 1;
        if (string.str[p] == '.')
        {
            result = Str8Skip(string, p + 1);
            break;
        }
    }
    return (result);
}

static PathStyle
path_style_from_str8(String8 string)
{
    PathStyle result = PathStyle_Relative;
    if (string.size >= 1 && string.str[0] == '/')
    {
        result = PathStyle_UnixAbsolute;
    }
    else if (string.size >= 2 && char_is_alpha(string.str[0]) && string.str[1] == ':')
    {
        if (string.size == 2 || char_is_slash(string.str[2]))
        {
            result = PathStyle_WindowsAbsolute;
        }
    }
    return (result);
}

static String8List
str8_split_path(Arena* arena, String8 string)
{
    String8List result = str8_split(arena, string, (U8*)"/\\", 2, 0);
    return (result);
}

static void
str8_path_list_resolve_dots_in_place(String8List* path, PathStyle style)
{
    Temp scratch = ScratchBegin(0, 0);

    String8MetaNode* stack = 0;
    String8MetaNode* free_meta_node = 0;
    String8Node* first = path->first;

    MemoryZeroStruct(path);
    for (String8Node *node = first, *next = 0; node != 0; node = next)
    {
        // save next now
        next = node->next;

        // cases:
        if (node == first && style == PathStyle_WindowsAbsolute)
        {
            goto save_without_stack;
        }
        if (node->string.size == 1 && node->string.str[0] == '.')
        {
            goto do_nothing;
        }
        if (node->string.size == 2 && node->string.str[0] == '.' && node->string.str[1] == '.')
        {
            if (stack != 0)
            {
                goto eliminate_stack_top;
            }
            else
            {
                goto save_without_stack;
            }
        }
        goto save_with_stack;

    // handlers:
    save_with_stack:
    {
        str8_list_push_node(path, node);

        String8MetaNode* stack_node = free_meta_node;
        if (stack_node != 0)
        {
            SLLStackPop(free_meta_node);
        }
        else
        {
            stack_node = PushArrayNoZero(scratch.arena, String8MetaNode, 1);
        }
        SLLStackPush(stack, stack_node);
        stack_node->node = node;

        continue;
    }

    save_without_stack:
    {
        str8_list_push_node(path, node);

        continue;
    }

    eliminate_stack_top:
    {
        path->node_count -= 1;
        path->total_size -= stack->node->string.size;

        SLLStackPop(stack);

        if (stack == 0)
        {
            path->last = path->first;
        }
        else
        {
            path->last = stack->node;
        }
        continue;
    }

    do_nothing:
        continue;
    }
    ScratchEnd(scratch);
}

static String8
str8_path_list_join_by_style(Arena* arena, String8List* path, PathStyle style)
{
    StringJoin params = {0};
    switch (style)
    {
    case PathStyle_Null:
    {
    }
    break;
    case PathStyle_Relative:
    case PathStyle_WindowsAbsolute:
    {
        params.sep = Str8Lit("/");
    }
    break;

    case PathStyle_UnixAbsolute:
    {
        params.pre = Str8Lit("/");
        params.sep = Str8Lit("/");
    }
    break;
    }
    String8 result = Str8ListJoin(arena, path, &params);
    return result;
}

static String8TxtPtPair
str8_txt_pt_pair_from_string(String8 string)
{
    String8TxtPtPair pair = {0};
    {
        String8 file_part = {0};
        String8 line_part = {0};
        String8 col_part = {0};

        // rjf: grab file part
        for (U64 idx = 0; idx <= string.size; idx += 1)
        {
            U8 byte = (idx < string.size) ? (string.str[idx]) : 0;
            U8 next_byte = ((idx + 1 < string.size) ? (string.str[idx + 1]) : 0);
            if (byte == ':' && next_byte != '/' && next_byte != '\\')
            {
                file_part = Str8Prefix(string, idx);
                line_part = Str8Skip(string, idx + 1);
                break;
            }
            else if (byte == 0)
            {
                file_part = string;
                break;
            }
        }

        // rjf: grab line/column
        {
            U64 colon_pos = FindSubstr8(line_part, Str8Lit(":"), 0, 0);
            if (colon_pos < line_part.size)
            {
                col_part = Str8Skip(line_part, colon_pos + 1);
                line_part = Str8Prefix(line_part, colon_pos);
            }
        }

        // rjf: convert line/column strings to numerics
        U64 line = 0;
        U64 column = 0;
        try_u64_from_str8_c_rules(line_part, &line);
        try_u64_from_str8_c_rules(col_part, &column);

        // rjf: fill
        pair.string = file_part;
        pair.pt = txt_pt((S64)line, (S64)column);
        if (pair.pt.line == 0)
        {
            pair.pt.line = 1;
        }
        if (pair.pt.column == 0)
        {
            pair.pt.column = 1;
        }
    }
    return pair;
}

////////////////////////////////
//~ rjf: UTF-8 & UTF-16 Decoding/Encoding

read_only static U8 utf8_class[32] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 3, 3, 4, 5,
};

static UnicodeDecode
utf8_decode(U8* str, U64 max)
{
    UnicodeDecode result = {1, max_U32};
    U8 byte = str[0];
    U8 byte_class = utf8_class[byte >> 3];
    switch (byte_class)
    {
    case 1:
    {
        result.codepoint = byte;
    }
    break;
    case 2:
    {
        if (2 < max)
        {
            U8 cont_byte = str[1];
            if (utf8_class[cont_byte >> 3] == 0)
            {
                result.codepoint = (byte & bitmask5) << 6;
                result.codepoint |= (cont_byte & bitmask6);
                result.inc = 2;
            }
        }
    }
    break;
    case 3:
    {
        if (2 < max)
        {
            U8 cont_byte[2] = {str[1], str[2]};
            if (utf8_class[cont_byte[0] >> 3] == 0 && utf8_class[cont_byte[1] >> 3] == 0)
            {
                result.codepoint = (byte & bitmask4) << 12;
                result.codepoint |= ((cont_byte[0] & bitmask6) << 6);
                result.codepoint |= (cont_byte[1] & bitmask6);
                result.inc = 3;
            }
        }
    }
    break;
    case 4:
    {
        if (3 < max)
        {
            U8 cont_byte[3] = {str[1], str[2], str[3]};
            if (utf8_class[cont_byte[0] >> 3] == 0 && utf8_class[cont_byte[1] >> 3] == 0 &&
                utf8_class[cont_byte[2] >> 3] == 0)
            {
                result.codepoint = (byte & bitmask3) << 18;
                result.codepoint |= ((cont_byte[0] & bitmask6) << 12);
                result.codepoint |= ((cont_byte[1] & bitmask6) << 6);
                result.codepoint |= (cont_byte[2] & bitmask6);
                result.inc = 4;
            }
        }
    }
    }
    return (result);
}

static UnicodeDecode
utf16_decode(U16* str, U64 max)
{
    UnicodeDecode result = {1, max_U32};
    result.codepoint = str[0];
    result.inc = 1;
    if (max > 1 && 0xD800 <= str[0] && str[0] < 0xDC00 && 0xDC00 <= str[1] && str[1] < 0xE000)
    {
        result.codepoint = ((str[0] - 0xD800) << 10) | ((str[1] - 0xDC00) + 0x10000);
        result.inc = 2;
    }
    return (result);
}

static U32
utf8_encode(U8* str, U32 codepoint)
{
    U32 inc = 0;
    if (codepoint <= 0x7F)
    {
        str[0] = (U8)codepoint;
        inc = 1;
    }
    else if (codepoint <= 0x7FF)
    {
        str[0] = (bitmask2 << 6) | ((codepoint >> 6) & bitmask5);
        str[1] = bit8 | (codepoint & bitmask6);
        inc = 2;
    }
    else if (codepoint <= 0xFFFF)
    {
        str[0] = (bitmask3 << 5) | ((codepoint >> 12) & bitmask4);
        str[1] = bit8 | ((codepoint >> 6) & bitmask6);
        str[2] = bit8 | (codepoint & bitmask6);
        inc = 3;
    }
    else if (codepoint <= 0x10FFFF)
    {
        str[0] = (bitmask4 << 4) | ((codepoint >> 18) & bitmask3);
        str[1] = bit8 | ((codepoint >> 12) & bitmask6);
        str[2] = bit8 | ((codepoint >> 6) & bitmask6);
        str[3] = bit8 | (codepoint & bitmask6);
        inc = 4;
    }
    else
    {
        str[0] = '?';
        inc = 1;
    }
    return (inc);
}

static U32
utf16_encode(U16* str, U32 codepoint)
{
    U32 inc = 1;
    if (codepoint == max_U32)
    {
        str[0] = (U16)'?';
    }
    else if (codepoint < 0x10000)
    {
        str[0] = (U16)codepoint;
    }
    else
    {
        U32 v = codepoint - 0x10000;
        str[0] = safe_cast_u16(0xD800 + (v >> 10));
        str[1] = safe_cast_u16(0xDC00 + (v & bitmask10));
        inc = 2;
    }
    return (inc);
}

static U32
utf8_from_utf32_single(U8* buffer, U32 character)
{
    return (utf8_encode(buffer, character));
}

////////////////////////////////
//~ rjf: Unicode String Conversions

static String8
str8_from_16(Arena* arena, String16 in)
{
    String8 result = Str8Zero();
    if (in.size)
    {
        U64 cap = in.size * 3;
        U8* str = PushArrayNoZero(arena, U8, cap + 1);
        U16* ptr = in.str;
        U16* opl = ptr + in.size;
        U64 size = 0;
        UnicodeDecode consume;
        for (; ptr < opl; ptr += consume.inc)
        {
            consume = utf16_decode(ptr, opl - ptr);
            size += utf8_encode(str + size, consume.codepoint);
        }
        str[size] = 0;
        arena_pop(arena, (cap - size));
        result = Str8(str, size);
    }
    return result;
}

static String16
Str16From8(Arena* arena, String8 in)
{
    String16 result = str16_zero();
    if (in.size)
    {
        U64 cap = in.size * 2;
        U16* str = PushArrayNoZero(arena, U16, cap + 1);
        U8* ptr = in.str;
        U8* opl = ptr + in.size;
        U64 size = 0;
        UnicodeDecode consume;
        for (; ptr < opl; ptr += consume.inc)
        {
            consume = utf8_decode(ptr, opl - ptr);
            size += utf16_encode(str + size, consume.codepoint);
        }
        str[size] = 0;
        arena_pop(arena, (cap - size) * 2);
        result = str16(str, size);
    }
    return result;
}

static String8
str8_from_32(Arena* arena, String32 in)
{
    String8 result = Str8Zero();
    if (in.size)
    {
        U64 cap = in.size * 4;
        U8* str = PushArrayNoZero(arena, U8, cap + 1);
        U32* ptr = in.str;
        U32* opl = ptr + in.size;
        U64 size = 0;
        for (; ptr < opl; ptr += 1)
        {
            size += utf8_encode(str + size, *ptr);
        }
        str[size] = 0;
        arena_pop(arena, (cap - size));
        result = Str8(str, size);
    }
    return result;
}

static String32
str32_from_8(Arena* arena, String8 in)
{
    String32 result = str32_zero();
    if (in.size)
    {
        U64 cap = in.size;
        U32* str = PushArrayNoZero(arena, U32, cap + 1);
        U8* ptr = in.str;
        U8* opl = ptr + in.size;
        U64 size = 0;
        UnicodeDecode consume;
        for (; ptr < opl; ptr += consume.inc)
        {
            consume = utf8_decode(ptr, opl - ptr);
            str[size] = consume.codepoint;
            size += 1;
        }
        str[size] = 0;
        arena_pop(arena, (cap - size) * 4);
        result = str32(str, size);
    }
    return result;
}

////////////////////////////////
//~ String -> Enum Conversions

read_only static struct
{
    String8 string;
    OperatingSystem os;
} g_os_enum_map[] = {
    {str8_lit_comp(""), OperatingSystem_Null},
    {
        str8_lit_comp("Windows"),
        OperatingSystem_Windows,
    },
    {
        str8_lit_comp("Linux"),
        OperatingSystem_Linux,
    },
    {
        str8_lit_comp("Mac"),
        OperatingSystem_Mac,
    },
};
// StaticAssert(ArrayCount(g_os_enum_map) == OperatingSystem_COUNT,
// g_os_enum_map_count_check);

static OperatingSystem
operating_system_from_string(String8 string)
{
    for (U64 i = 0; i < ArrayCount(g_os_enum_map); ++i)
    {
        if (Str8Match(g_os_enum_map[i].string, string, MatchFlag_CaseInsensitive))
        {
            return g_os_enum_map[i].os;
        }
    }
    return OperatingSystem_Null;
}

////////////////////////////////
//~ rjf: Basic Types & Space Enum -> String Conversions

static String8
string_from_dimension(Dimension dimension)
{
    local_persist String8 strings[] = {
        str8_lit_comp("X"),
        str8_lit_comp("Y"),
        str8_lit_comp("Z"),
        str8_lit_comp("W"),
    };
    String8 result = Str8Lit("error");
    if ((U32)dimension < 4)
    {
        result = strings[dimension];
    }
    return (result);
}

static String8
string_from_side(Side side)
{
    local_persist String8 strings[] = {
        str8_lit_comp("Min"),
        str8_lit_comp("Max"),
    };
    String8 result = Str8Lit("error");
    if ((U32)side < 2)
    {
        result = strings[side];
    }
    return (result);
}

static String8
string_from_operating_system(OperatingSystem os)
{
    String8 result = g_os_enum_map[OperatingSystem_Null].string;
    if (os < ArrayCount(g_os_enum_map))
    {
        result = g_os_enum_map[os].string;
    }
    return result;
}

static String8
string_from_arch(Arch arch)
{
    local_persist String8 strings[] = {
        str8_lit_comp("Null"),  str8_lit_comp("x64"),   str8_lit_comp("x86"),
        str8_lit_comp("arm64"), str8_lit_comp("arm32"),
    };
    String8 result = Str8Lit("error");
    if (arch < Arch_COUNT)
    {
        result = strings[arch];
    }
    return (result);
}

////////////////////////////////
//~ rjf: Time Types -> String

static String8
string_from_week_day(WeekDay week_day)
{
    local_persist String8 strings[] = {
        str8_lit_comp("Sun"), str8_lit_comp("Mon"), str8_lit_comp("Tue"), str8_lit_comp("Wed"),
        str8_lit_comp("Thu"), str8_lit_comp("Fri"), str8_lit_comp("Sat"),
    };
    String8 result = Str8Lit("Err");
    if ((U32)week_day < WeekDay_COUNT)
    {
        result = strings[week_day];
    }
    return (result);
}

static String8
string_from_month(Month month)
{
    local_persist String8 strings[] = {
        str8_lit_comp("Jan"), str8_lit_comp("Feb"), str8_lit_comp("Mar"), str8_lit_comp("Apr"),
        str8_lit_comp("May"), str8_lit_comp("Jun"), str8_lit_comp("Jul"), str8_lit_comp("Aug"),
        str8_lit_comp("Sep"), str8_lit_comp("Oct"), str8_lit_comp("Nov"), str8_lit_comp("Dec"),
    };
    String8 result = Str8Lit("Err");
    if ((U32)month < Month_COUNT)
    {
        result = strings[month];
    }
    return (result);
}

static String8
push_date_time_string(Arena* arena, DateTime* date_time)
{
    char* mon_str = (char*)string_from_month(date_time->month).str;
    U32 adjusted_hour = date_time->hour % 12;
    if (adjusted_hour == 0)
    {
        adjusted_hour = 12;
    }
    const char* ampm = "am";
    if (date_time->hour >= 12)
    {
        ampm = "pm";
    }
    String8 result =
        push_str8f(arena, "%d %s %d, %02d:%02d:%02d %s", date_time->day, mon_str, date_time->year,
                   adjusted_hour, date_time->min, date_time->sec, ampm);
    return (result);
}

static String8
push_file_name_date_time_string(Arena* arena, DateTime* date_time)
{
    char* mon_str = (char*)string_from_month(date_time->month).str;
    String8 result = push_str8f(arena, "%d-%s-%0d--%02d-%02d-%02d", date_time->year, mon_str,
                                date_time->day, date_time->hour, date_time->min, date_time->sec);
    return (result);
}

static String8
string_from_elapsed_time(Arena* arena, DateTime dt)
{
    Temp scratch = ScratchBegin(&arena, 1);
    String8List list = {0};
    if (dt.year)
    {
        Str8ListPushF(scratch.arena, &list, "%dy", dt.year);
        Str8ListPushF(scratch.arena, &list, "%um", dt.mon);
        Str8ListPushF(scratch.arena, &list, "%ud", dt.day);
    }
    else if (dt.mon)
    {
        Str8ListPushF(scratch.arena, &list, "%um", dt.mon);
        Str8ListPushF(scratch.arena, &list, "%ud", dt.day);
    }
    else if (dt.day)
    {
        Str8ListPushF(scratch.arena, &list, "%ud", dt.day);
    }
    Str8ListPushF(scratch.arena, &list, "%u:%u:%u:%u ms", dt.hour, dt.min, dt.sec, dt.msec);
    StringJoin join = {str8_lit_comp(""), str8_lit_comp(" "), str8_lit_comp("")};
    String8 result = Str8ListJoin(arena, &list, &join);
    ScratchEnd(scratch);
    return (result);
}

////////////////////////////////
//~ Globally UNique Ids

static String8
string_from_guid(Arena* arena, Guid guid)
{
    String8 result =
        push_str8f(arena, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", guid.data1,
                   guid.data2, guid.data3, guid.data4[0], guid.data4[1], guid.data4[2],
                   guid.data4[3], guid.data4[4], guid.data4[5], guid.data4[6], guid.data4[7]);
    return result;
}

static B32
try_guid_from_string(String8 string, Guid* guid_out)
{
    Temp scratch = ScratchBegin(0, 0);
    B32 is_parsed = 0;
    String8List list = str8_split_by_string_chars(scratch.arena, string, Str8Lit("-"),
                                                  StringSplitFlag_KeepEmpties);
    if (list.node_count == 5)
    {
        String8 data1_str = list.first->string;
        String8 data2_str = list.first->next->string;
        String8 data3_str = list.first->next->next->string;
        String8 data4_hi_str = list.first->next->next->next->string;
        String8 data4_lo_str = list.first->next->next->next->next->string;
        if (str8_is_integer(data1_str, 16) && str8_is_integer(data2_str, 16) &&
            str8_is_integer(data3_str, 16) && str8_is_integer(data4_hi_str, 16) &&
            str8_is_integer(data4_lo_str, 16))
        {
            U64 data1 = U64FromStr8(data1_str, 16);
            U64 data2 = U64FromStr8(data2_str, 16);
            U64 data3 = U64FromStr8(data3_str, 16);
            U64 data4_hi = U64FromStr8(data4_hi_str, 16);
            U64 data4_lo = U64FromStr8(data4_lo_str, 16);
            if (data1 <= max_U32 && data2 <= max_U16 && data3 <= max_U16 && data4_hi <= max_U16 &&
                data4_lo <= 0xffffffffffff)
            {
                guid_out->data1 = (U32)data1;
                guid_out->data2 = (U16)data2;
                guid_out->data3 = (U16)data3;
                U64 data4 = (data4_hi << 48) | data4_lo;
                MemoryCopy(&guid_out->data4[0], &data4, sizeof(data4));
                is_parsed = 1;
            }
        }
    }
    ScratchEnd(scratch);
    return is_parsed;
}

static Guid
guid_from_string(String8 string)
{
    Guid guid = {0};
    try_guid_from_string(string, &guid);
    return guid;
}

////////////////////////////////
//~ rjf: Basic Text Indentation

static String8
indented_from_string(Arena* arena, String8 string)
{
    Temp scratch = ScratchBegin(&arena, 1);
    read_only local_persist U8 indentation_bytes[] =
        "                                                                        "
        "                  "
        "                                      ";
    String8List indented_strings = {0};
    S64 depth = 0;
    S64 next_depth = 0;
    U64 line_begin_off = 0;
    for (U64 off = 0; off <= string.size; off += 1)
    {
        U8 byte = off < string.size ? string.str[off] : 0;
        switch (byte)
        {
        default:
        {
        }
        break;
        case '{':
        case '[':
        case '(':
        {
            next_depth += 1;
            next_depth = Max(0, next_depth);
        }
        break;
        case '}':
        case ']':
        case ')':
        {
            next_depth -= 1;
            next_depth = Max(0, next_depth);
            depth = next_depth;
        }
        break;
        case '\n':
        case 0:
        {
            String8 line =
                str8_skip_chop_whitespace(str8_substr(string, r1u64(line_begin_off, off)));
            if (line.size != 0)
            {
                Str8ListPushF(scratch.arena, &indented_strings, "%.*s%s\n", (int)depth * 2,
                              indentation_bytes, (char*)line.str);
            }
            if (line.size == 0 && indented_strings.node_count != 0 && off < string.size)
            {
                Str8ListPushF(scratch.arena, &indented_strings, "\n");
            }
            line_begin_off = off + 1;
            depth = next_depth;
        }
        break;
        }
    }
    String8 result = Str8ListJoin(arena, &indented_strings, 0);
    ScratchEnd(scratch);
    return result;
}

////////////////////////////////
//~ rjf: Text Escaping

static String8
escaped_from_raw_str8(Arena* arena, String8 string)
{
    Temp scratch = ScratchBegin(&arena, 1);
    String8List parts = {0};
    U64 start_split_idx = 0;
    for (U64 idx = 0; idx <= string.size; idx += 1)
    {
        U8 byte = (idx < string.size) ? string.str[idx] : 0;
        B32 split = 1;
        String8 separator_replace = {0};
        switch (byte)
        {
        default:
        {
            split = 0;
        }
        break;
        case 0:
        {
        }
        break;
        case '\a':
        {
            separator_replace = Str8Lit("\\a");
        }
        break;
        case '\b':
        {
            separator_replace = Str8Lit("\\b");
        }
        break;
        case '\f':
        {
            separator_replace = Str8Lit("\\f");
        }
        break;
        case '\n':
        {
            separator_replace = Str8Lit("\\n");
        }
        break;
        case '\r':
        {
            separator_replace = Str8Lit("\\r");
        }
        break;
        case '\t':
        {
            separator_replace = Str8Lit("\\t");
        }
        break;
        case '\v':
        {
            separator_replace = Str8Lit("\\v");
        }
        break;
        case '\\':
        {
            separator_replace = Str8Lit("\\\\");
        }
        break;
        case '"':
        {
            separator_replace = Str8Lit("\\\"");
        }
        break;
        }
        if (split)
        {
            String8 substr = str8_substr(string, r1u64(start_split_idx, idx));
            start_split_idx = idx + 1;
            Str8ListPush(scratch.arena, &parts, substr);
            if (separator_replace.size != 0)
            {
                Str8ListPush(scratch.arena, &parts, separator_replace);
            }
        }
    }
    StringJoin join = {0};
    String8 result = Str8ListJoin(arena, &parts, &join);
    ScratchEnd(scratch);
    return result;
}

static String8
raw_from_escaped_str8(Arena* arena, String8 string)
{
    Temp scratch = ScratchBegin(&arena, 1);
    String8List strs = {0};
    U64 start = 0;
    for (U64 idx = 0; idx <= string.size; idx += 1)
    {
        if (idx == string.size || string.str[idx] == '\\' || string.str[idx] == '\r')
        {
            String8 str = str8_substr(string, r1u64(start, idx));
            if (str.size != 0)
            {
                Str8ListPush(scratch.arena, &strs, str);
            }
            start = idx + 1;
        }
        if (idx < string.size && string.str[idx] == '\\')
        {
            U8 next_char = string.str[idx + 1];
            U8 replace_byte = 0;
            switch (next_char)
            {
            default:
            {
            }
            break;
            case 'a':
                replace_byte = 0x07;
                break;
            case 'b':
                replace_byte = 0x08;
                break;
            case 'e':
                replace_byte = 0x1b;
                break;
            case 'f':
                replace_byte = 0x0c;
                break;
            case 'n':
                replace_byte = 0x0a;
                break;
            case 'r':
                replace_byte = 0x0d;
                break;
            case 't':
                replace_byte = 0x09;
                break;
            case 'v':
                replace_byte = 0x0b;
                break;
            case '\\':
                replace_byte = '\\';
                break;
            case '\'':
                replace_byte = '\'';
                break;
            case '"':
                replace_byte = '"';
                break;
            case '?':
                replace_byte = '?';
                break;
            }
            String8 replace_string = PushStr8Copy(scratch.arena, Str8(&replace_byte, 1));
            Str8ListPush(scratch.arena, &strs, replace_string);
            idx += 1;
            start += 1;
        }
    }
    String8 result = Str8ListJoin(arena, &strs, 0);
    ScratchEnd(scratch);
    return result;
}

////////////////////////////////
//~ rjf: Text Wrapping

static String8List
wrapped_lines_from_string(Arena* arena, String8 string, U64 first_line_max_width, U64 max_width,
                          U64 wrap_indent)
{
    String8List list = {0};
    Rng1U64 line_range = r1u64(0, 0);
    U64 wrapped_indent_level = 0;
    static const char* spaces = "                                                                ";
    for (U64 idx = 0; idx <= string.size; idx += 1)
    {
        U8 chr = idx < string.size ? string.str[idx] : 0;
        if (chr == '\n')
        {
            Rng1U64 candidate_line_range = line_range;
            candidate_line_range.max = idx;
            // NOTE(nick): when wrapping is interrupted with \n we emit a string
            // without including
            // \n because later tool_fprint_list inserts separator after each node
            // except for last node, so don't strip last \n.
            if (idx + 1 == string.size)
            {
                candidate_line_range.max += 1;
            }
            String8 substr = str8_substr(string, candidate_line_range);
            Str8ListPush(arena, &list, substr);
            line_range = r1u64(idx + 1, idx + 1);
        }
        else if (char_is_space(chr) || chr == 0)
        {
            Rng1U64 candidate_line_range = line_range;
            candidate_line_range.max = idx;
            String8 substr = str8_substr(string, candidate_line_range);
            U64 width_this_line = max_width - wrapped_indent_level;
            if (list.node_count == 0)
            {
                width_this_line = first_line_max_width;
            }
            if (substr.size > width_this_line)
            {
                String8 line = str8_substr(string, line_range);
                if (wrapped_indent_level > 0)
                {
                    line = push_str8f(arena, "%.*s%s", wrapped_indent_level, spaces, line.str);
                }
                Str8ListPush(arena, &list, line);
                line_range = r1u64(line_range.max + 1, candidate_line_range.max);
                wrapped_indent_level = ClampTop(64, wrap_indent);
            }
            else
            {
                line_range = candidate_line_range;
            }
        }
    }
    if (line_range.min < string.size && line_range.max > line_range.min)
    {
        String8 line = str8_substr(string, line_range);
        if (wrapped_indent_level > 0)
        {
            line = push_str8f(arena, "%.*s%s", wrapped_indent_level, spaces, line.str);
        }
        Str8ListPush(arena, &list, line);
    }
    return list;
}

////////////////////////////////
//~ rjf: String <-> Color

static String8
hex_string_from_rgba_4f32(Arena* arena, Vec4F32 rgba)
{
    String8 hex_string =
        push_str8f(arena, "%02x%02x%02x%02x", (U8)(rgba.x * 255.f), (U8)(rgba.y * 255.f),
                   (U8)(rgba.z * 255.f), (U8)(rgba.w * 255.f));
    return hex_string;
}

static Vec4F32
rgba_from_hex_string_4f32(String8 hex_string)
{
    U8 byte_text[8] = {0};
    U64 byte_text_idx = 0;
    for (U64 idx = 0; idx < hex_string.size && byte_text_idx < ArrayCount(byte_text); idx += 1)
    {
        if (char_is_digit(hex_string.str[idx], 16))
        {
            byte_text[byte_text_idx] = char_to_lower(hex_string.str[idx]);
            byte_text_idx += 1;
        }
    }
    U8 byte_vals[4] = {0};
    for (U64 idx = 0; idx < 4; idx += 1)
    {
        byte_vals[idx] = (U8)U64FromStr8(Str8(&byte_text[idx * 2], 2), 16);
    }
    Vec4F32 rgba = v4f32(byte_vals[0] / 255.f, byte_vals[1] / 255.f, byte_vals[2] / 255.f,
                         byte_vals[3] / 255.f);
    return rgba;
}

////////////////////////////////
//~ rjf: String Fuzzy Matching

static FuzzyMatchRangeList
fuzzy_match_find(Arena* arena, String8 needle, String8 haystack)
{
    FuzzyMatchRangeList result = {0};
    Temp scratch = ScratchBegin(&arena, 1);
    String8List needles = str8_split(scratch.arena, needle, (U8*)" ", 1, 0);
    result.needle_part_count = needles.node_count;
    for (String8Node* needle_n = needles.first; needle_n != 0; needle_n = needle_n->next)
    {
        U64 find_pos = 0;
        for (; find_pos < haystack.size;)
        {
            find_pos = FindSubstr8(haystack, needle_n->string, find_pos, MatchFlag_CaseInsensitive);
            B32 is_in_gathered_ranges = 0;
            for (FuzzyMatchRangeNode* n = result.first; n != 0; n = n->next)
            {
                if (n->range.min <= find_pos && find_pos < n->range.max)
                {
                    is_in_gathered_ranges = 1;
                    find_pos = n->range.max;
                    break;
                }
            }
            if (!is_in_gathered_ranges)
            {
                break;
            }
        }
        if (find_pos < haystack.size)
        {
            Rng1U64 range = r1u64(find_pos, find_pos + needle_n->string.size);
            FuzzyMatchRangeNode* n = PushArray(arena, FuzzyMatchRangeNode, 1);
            n->range = range;
            SLLQueuePush(result.first, result.last, n);
            result.count += 1;
            result.total_dim += dim_1u64(range);
        }
    }
    ScratchEnd(scratch);
    return result;
}

static FuzzyMatchRangeList
fuzzy_match_range_list_copy(Arena* arena, FuzzyMatchRangeList* src)
{
    FuzzyMatchRangeList dst = {0};
    for (FuzzyMatchRangeNode* src_n = src->first; src_n != 0; src_n = src_n->next)
    {
        FuzzyMatchRangeNode* dst_n = PushArray(arena, FuzzyMatchRangeNode, 1);
        SLLQueuePush(dst.first, dst.last, dst_n);
        dst_n->range = src_n->range;
    }
    dst.count = src->count;
    dst.needle_part_count = src->needle_part_count;
    dst.total_dim = src->total_dim;
    return dst;
}

////////////////////////////////
//~ NOTE(allen): Serialization Helpers

static void
str8_serial_begin(Arena* arena, String8List* srl)
{
    String8Node* node = PushArray(arena, String8Node, 1);
    node->string.str = PushArrayNoZero(arena, U8, 0);
    srl->first = srl->last = node;
    srl->node_count = 1;
    srl->total_size = 0;
}

static String8
str8_serial_end(Arena* arena, String8List* srl)
{
    U64 size = srl->total_size;
    U8* out = PushArrayNoZero(arena, U8, size);
    str8_serial_write_to_dst(srl, out);
    String8 result = Str8(out, size);
    return result;
}

static void
str8_serial_write_to_dst(String8List* srl, void* out)
{
    U8* ptr = (U8*)out;
    for (String8Node* node = srl->first; node != 0; node = node->next)
    {
        U64 size = node->string.size;
        MemoryCopy(ptr, node->string.str, size);
        ptr += size;
    }
}

static U64
str8_serial_push_align(Arena* arena, String8List* srl, U64 align)
{
    Assert(IsPow2(align));

    U64 pos = srl->total_size;
    U64 new_pos = AlignPow2(pos, align);
    U64 size = (new_pos - pos);

    if (size != 0)
    {
        U8* buf = PushArray(arena, U8, size);

        String8* str = &srl->last->string;
        if (str->str + str->size == buf)
        {
            srl->last->string.size += size;
            srl->total_size += size;
        }
        else
        {
            Str8ListPush(arena, srl, Str8(buf, size));
        }
    }
    return size;
}

static void*
str8_serial_push_size(Arena* arena, String8List* srl, U64 size)
{
    void* result = 0;
    if (size != 0)
    {
        U8* buf = PushArrayNoZero(arena, U8, size);
        String8* str = &srl->last->string;
        if (str->str + str->size == buf)
        {
            srl->last->string.size += size;
            srl->total_size += size;
        }
        else
        {
            Str8ListPush(arena, srl, Str8(buf, size));
        }
        result = buf;
    }
    return result;
}

static void*
str8_serial_push_data(Arena* arena, String8List* srl, void* data, U64 size)
{
    void* result = str8_serial_push_size(arena, srl, size);
    if (result != 0)
    {
        MemoryCopy(result, data, size);
    }
    return result;
}

static void
str8_serial_push_data_list(Arena* arena, String8List* srl, String8Node* first)
{
    for (String8Node* node = first; node != 0; node = node->next)
    {
        str8_serial_push_data(arena, srl, node->string.str, node->string.size);
    }
}

static void
str8_serial_push_u64(Arena* arena, String8List* srl, U64 x)
{
    U8* buf = PushArrayNoZero(arena, U8, 8);
    MemoryCopy(buf, &x, 8);
    String8* str = &srl->last->string;
    if (str->str + str->size == buf)
    {
        srl->last->string.size += 8;
        srl->total_size += 8;
    }
    else
    {
        Str8ListPush(arena, srl, Str8(buf, 8));
    }
}

static void
str8_serial_push_u32(Arena* arena, String8List* srl, U32 x)
{
    U8* buf = PushArrayNoZero(arena, U8, 4);
    MemoryCopy(buf, &x, 4);
    String8* str = &srl->last->string;
    if (str->str + str->size == buf)
    {
        srl->last->string.size += 4;
        srl->total_size += 4;
    }
    else
    {
        Str8ListPush(arena, srl, Str8(buf, 4));
    }
}

static void
str8_serial_push_u16(Arena* arena, String8List* srl, U16 x)
{
    str8_serial_push_data(arena, srl, &x, sizeof(x));
}

static void
str8_serial_push_u8(Arena* arena, String8List* srl, U8 x)
{
    str8_serial_push_data(arena, srl, &x, sizeof(x));
}

static void
str8_serial_push_cstr(Arena* arena, String8List* srl, String8 str)
{
    str8_serial_push_data(arena, srl, str.str, str.size);
    str8_serial_push_u8(arena, srl, 0);
}

static void
str8_serial_push_string(Arena* arena, String8List* srl, String8 str)
{
    str8_serial_push_data(arena, srl, str.str, str.size);
}

////////////////////////////////
//~ rjf: Deserialization Helpers

static U64
str8_deserial_read(String8 string, U64 off, void* read_dst, U64 read_size, U64 granularity)
{
    U64 bytes_left = string.size - Min(off, string.size);
    U64 actually_readable_size = Min(bytes_left, read_size);
    U64 legally_readable_size = actually_readable_size - actually_readable_size % granularity;
    if (legally_readable_size > 0)
    {
        MemoryCopy(read_dst, string.str + off, legally_readable_size);
    }
    return legally_readable_size;
}

static U64
str8_deserial_find_first_match(String8 string, U64 off, U16 scan_val)
{
    U64 cursor = off;
    for (;;)
    {
        U16 val = 0;
        str8_deserial_read_struct(string, cursor, &val);
        if (val == scan_val)
        {
            break;
        }
        cursor += sizeof(val);
    }
    return cursor;
}

static void*
str8_deserial_get_raw_ptr(String8 string, U64 off, U64 size)
{
    void* raw_ptr = 0;
    if (off + size <= string.size)
    {
        raw_ptr = string.str + off;
    }
    return raw_ptr;
}

static U64
str8_deserial_read_cstr(String8 string, U64 off, String8* cstr_out)
{
    U64 cstr_size = 0;
    if (off < string.size)
    {
        U8* ptr = string.str + off;
        U8* cap = string.str + string.size;
        *cstr_out = str8_cstring_capped(ptr, cap);
        cstr_size = (cstr_out->size + 1);
    }
    return cstr_size;
}

static U64
str8_deserial_read_windows_utf16_string16(String8 string, U64 off, String16* str_out)
{
    U64 null_off = str8_deserial_find_first_match(string, off, 0);
    U64 size = null_off - off;
    U16* str = (U16*)str8_deserial_get_raw_ptr(string, off, size);
    U64 count = size / sizeof(*str);
    *str_out = str16(str, count);

    U64 read_size_with_null = size + sizeof(*str);
    return read_size_with_null;
}

static U64
str8_deserial_read_block(String8 string, U64 off, U64 size, String8* block_out)
{
    Rng1U64 range = rng_1u64(off, off + size);
    *block_out = str8_substr(string, range);
    return block_out->size;
}

//- rjf: Allocation
static String8
PushStr8Copy(Arena* arena, String8 string)
{
    String8 res;
    res.size = string.size;
    res.str = PushArrayNoZero(arena, U8, string.size + 1);
    MemoryCopy(res.str, string.str, string.size);
    res.str[string.size] = 0;
    return res;
}

static String8
PushStr8FV(Arena* arena, const char* fmt, va_list args)
{
    String8 result = {0};
    va_list args2;
    va_copy(args2, args);
    U64 needed_bytes = vsnprintf(0, 0, fmt, args) + 1;
    result.str = PushArrayNoZero(arena, U8, needed_bytes);
    result.size = needed_bytes - 1;
    vsnprintf((char*)result.str, needed_bytes, fmt, args2);
    return result;
}

static String8
PushStr8F(Arena* arena, const char* fmt, ...)
{
    String8 result = {0};
    va_list args;
    va_start(args, fmt);
    result = PushStr8FV(arena, fmt, args);
    va_end(args);
    return result;
}

static String8
PushStr8FillByte(Arena* arena, U64 size, U8 byte)
{
    String8 result = {0};
    result.str = PushArrayNoZero(arena, U8, size);
    MemorySet(result.str, byte, size);
    result.size = size;
    return result;
}

// ~mgj: String <==> float/double copy
B32
F32FromStr8(String8 string, F32* result)
{
    char* is_not_success = 0;
    *result = strtof((char*)string.str, &is_not_success);
    B32 ret = !(*is_not_success);
    return ret;
}

// ~mgj: String hashes
static U128
HashU128FromStr8(String8 str)
{
    ScratchScope scratch = ScratchScope(0, 0);

    U64 align = 16;
    U64 size_to_alignment = str.size + (align - (str.size % align));
    U8* buffer = PushArrayAligned(scratch.arena, U8, size_to_alignment, align);
    MemoryCopy(buffer, str.str, str.size);
    meow_u128 hash = MeowHash(MeowDefaultSeed, str.size, buffer);
    U128 result;
    MemoryCopy(&result, &hash, sizeof(result));
    return result;
}
