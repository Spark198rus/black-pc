#include "language.h"

#include <string>

int main()
{
    using namespace language;
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
    CHECK(is_supported("en"));
    CHECK(is_supported("pt-BR"));
    CHECK(is_supported("es"));
    CHECK(is_supported("fr"));
    CHECK(is_supported("de"));
    CHECK(is_supported("it"));
    CHECK(!is_supported("pt"));
    CHECK(!is_supported("xx"));
    CHECK(!is_supported(""));
    CHECK(normalize("pt-BR") == "pt-BR");
    CHECK(normalize("PT-br") == "en");
    CHECK(normalize("invalid") == "en");
    CHECK(locale_count() == 6);
    CHECK(std::string(locale_id(0)) == "en");
    CHECK(std::string(locale_id(1)) == "pt-BR");
    CHECK(std::string(locale_id(5)) == "it");
    CHECK(std::string(locale_id(-1)) == "en");
    CHECK(std::string(locale_id(6)) == "en");
    CHECK(std::wstring(display_name(0)) == L"ENGLISH");
    CHECK(std::wstring(display_name(1)) == L"PORTUGUÊS (BRASIL)");
    CHECK(std::wstring(display_name(2)) == L"ESPAÑOL");
    CHECK(std::wstring(display_name(3)) == L"FRANÇAIS");
    CHECK(std::wstring(display_name(4)) == L"DEUTSCH");
    CHECK(std::wstring(display_name(5)) == L"ITALIANO");
    CHECK(std::string(group_title(4, "pt-BR")) == "IDIOMAS");
    CHECK(std::string(language_row_caption("it")) == "LINGUA");
    CHECK(std::wstring(pc_settings_title("pt-BR")) == L"CONFIGURAÇÕES DO PC");
}
