#include "util/prompt_text.h"

namespace util {

std::wstring expand_placeholders(const std::wstring &in, size_t max_chars, const std::function<std::wstring(wchar_t)> &label,
                                 const std::function<std::wstring(wchar_t)> &fallback)
{
    std::wstring out;
    for (size_t i = 0; i < in.size(); i++) {
        wchar_t c = in[i];
        if (c < kFirstPlaceholder || c > kLastPlaceholder) {
            out += c;
            continue;
        }
        std::wstring s = label(c);
        size_t rest = in.size() - i - 1; // never longer than the caller's field: a label that does not fit falls back
        if (s.empty() || out.size() + s.size() + rest >= max_chars) s = fallback(c);
        out += s;
    }
    return out;
}

} // namespace util
