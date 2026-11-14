#include "check.h"

#include "util/prompt_text.h"

using util::expand_placeholders;

namespace {

std::wstring label_r(wchar_t c) { return c == 0xF00B ? L"[R]" : L""; }
std::wstring glyph(wchar_t) { return L"\x00B0"; }

} // namespace

TEST(prompt_text_without_placeholders_is_unchanged)
{
    CHECK(expand_placeholders(L"Nothing to see", 0x78, label_r, glyph) == L"Nothing to see");
    CHECK(expand_placeholders(L"", 0x78, label_r, glyph) == L"");
    // just outside the range: U+F000 and U+F100 are not placeholders
    CHECK(expand_placeholders(L"\xF000\xF100", 0x78, label_r, glyph) == L"\xF000\xF100");
}

TEST(prompt_text_uses_the_label)
{
    CHECK(expand_placeholders(L"Press \xF00B to reload", 0x78, label_r, glyph) == L"Press [R] to reload");
    CHECK(expand_placeholders(L"\xF00B\xF00B", 0x78, label_r, glyph) == L"[R][R]");
}

TEST(prompt_text_falls_back_without_a_label)
{
    CHECK(expand_placeholders(L"Throw \xF01B", 0x78, label_r, glyph) == L"Throw \x00B0");
}

TEST(prompt_text_falls_back_when_too_long)
{
    // "ab" + "[R]" + "c" = 6 characters + NUL: fits a field of 7, not one of 6
    CHECK(expand_placeholders(L"ab\xF00B" L"c", 7, label_r, glyph) == L"ab[R]c");
    CHECK(expand_placeholders(L"ab\xF00B" L"c", 6, label_r, glyph) == L"ab\x00B0" L"c");
    // the room left counts the text still to come, so the result always fits the field
    auto big = [](wchar_t) -> std::wstring { return L"[LEFT MOUSE]"; };
    std::wstring out = expand_placeholders(L"\xF001 and \xF002", 20, big, glyph);
    CHECK(out.size() < 20);
    CHECK(out == L"[LEFT MOUSE] and \x00B0");
}
