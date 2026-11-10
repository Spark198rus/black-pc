#include "check.h"

#include "util/files.h"
#include "util/text.h"

#include <windows.h>

#include <string>

TEST(files_round_trip_and_create_folders)
{
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring root = std::wstring(tmp) + L"blackutil_tests_" + std::to_wstring(GetCurrentProcessId());
    std::wstring path = root + L"\\a\\b\\file.bin";
    std::vector<uint8_t> data = {0, 1, 2, 0xFF, 0x1A, 0x0D, 0x0A}; // binary: no newline or Ctrl-Z translation
    REQUIRE(util::write_file(path, data));
    std::vector<uint8_t> back;
    CHECK(util::read_file(path, back));
    CHECK(back == data);
    CHECK(!util::read_file(root + L"\\missing.bin", back));
    DeleteFileW(path.c_str());
    RemoveDirectoryW((root + L"\\a\\b").c_str());
    RemoveDirectoryW((root + L"\\a").c_str());
    RemoveDirectoryW(root.c_str());
}

TEST(text_utf8_round_trip)
{
    std::wstring w = L"A\x00E7\x00E3o \x2192 \xE000"; // "Ação", an arrow and a private-use icon character
    std::string s = narrow(w);
    CHECK(s == "A\xC3\xA7\xC3\xA3o \xE2\x86\x92 \xEE\x80\x80");
    CHECK(widen(s) == w);
    CHECK(narrow(L"").empty());
    CHECK(widen("").empty());
}
