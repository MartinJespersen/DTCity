TEST_CASE("char classification")
{
    CHECK(char_is_alpha('a'));
    CHECK(char_is_alpha('Z'));
    CHECK_FALSE(char_is_alpha('1'));
    CHECK_FALSE(char_is_alpha('!'));

    CHECK(char_is_digit('0', 10));
    CHECK(char_is_digit('9', 10));
    CHECK_FALSE(char_is_digit('a', 10));

    CHECK(char_is_space(' '));
    CHECK(char_is_space('\t'));
    CHECK_FALSE(char_is_space('a'));
}

TEST_CASE("char conversion")
{
    CHECK(char_to_lower('A') == 'a');
    CHECK(char_to_lower('a') == 'a');

    CHECK(char_to_upper('a') == 'A');
    CHECK(char_to_upper('A') == 'A');
}

TEST_CASE("String8 construction")
{
    String8 s = str8_lit("hello");
    CHECK(s.size == 5);
    CHECK(s.str[0] == 'h');
    CHECK(s.str[4] == 'o');

    String8 empty = Str8Zero();
    CHECK(empty.size == 0);
    CHECK(empty.str == nullptr);
}

TEST_CASE("str8_match")
{
    String8 a = str8_lit("hello");
    String8 b = str8_lit("hello");
    String8 c = str8_lit("world");
    String8 d = str8_lit("HELLO");

    CHECK(str8_match(a, b, 0));
    CHECK_FALSE(str8_match(a, c, 0));
    CHECK_FALSE(str8_match(a, d, 0));
    CHECK(str8_match(a, d, MatchFlag_CaseInsensitive));
}
