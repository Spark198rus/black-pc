// The placeholder expansion of the game's hint texts (input/prompts.cpp), without the game: what a placeholder becomes
// is up to the caller.
#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace util {

constexpr wchar_t kFirstPlaceholder = 0xF001, kLastPlaceholder = 0xF0FF;

// Each placeholder becomes label(c), or fallback(c) when the label is empty or the text would no longer fit in a field
// of max_chars characters (with its NUL). Other characters are kept.
std::wstring expand_placeholders(const std::wstring &in, size_t max_chars, const std::function<std::wstring(wchar_t)> &label,
                                 const std::function<std::wstring(wchar_t)> &fallback);

} // namespace util
