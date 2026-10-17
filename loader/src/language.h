#pragma once

#include <string>

namespace language {

int locale_count();
const char *locale_id(int index);
const wchar_t *display_name(int index);
bool is_supported(const std::string &id);
std::string normalize(const std::string &id);
int locale_index(const std::string &id);
const char *group_title(int group, const std::string &locale);
const char *language_row_caption(const std::string &locale);
const wchar_t *language_row_label(const std::string &locale);
const wchar_t *pc_settings_title(const std::string &locale);

} // namespace language
