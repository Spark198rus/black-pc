// Whole-file reads and writes for the rebuilt game files (fe/pages.cpp, fe/fonts.cpp).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace util {

bool read_file(const std::wstring &path, std::vector<uint8_t> &out);
// Creates each folder on the way; false if the file could not be written.
bool write_file(const std::wstring &path, const std::vector<uint8_t> &data);

} // namespace util
