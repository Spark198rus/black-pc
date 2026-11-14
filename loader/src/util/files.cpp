#include "util/files.h"

#include <windows.h>

#include <fstream>
#include <iterator>

namespace util {

bool read_file(const std::wstring &path, std::vector<uint8_t> &out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

bool write_file(const std::wstring &path, const std::vector<uint8_t> &data)
{
    for (size_t i = path.find(L'\\', 3); i != std::wstring::npos; i = path.find(L'\\', i + 1))
        CreateDirectoryW(path.substr(0, i).c_str(), nullptr); // each folder on the way
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write((const char *)data.data(), (std::streamsize)data.size());
    f.close();
    return (bool)f;
}

} // namespace util
