// See language.h: the system language selects the game's text and its loading video on the PAL disc.
#include "language.h"

#include "common.h"

namespace {

// Xbox XC_LANGUAGE values.
enum {
    kEnglish = 1,
    kJapanese = 2,
    kGerman = 3,
    kFrench = 4,
    kSpanish = 5,
    kItalian = 6,
    kKorean = 7,
    kChinese = 8,
};

struct Language {
    const char *name; // short name: the ini, --language and the logs
    int index;        // XC_LANGUAGE value
};

// What the port can ask for. The PAL disc carries English (MainUK), German, French, Spanish and Italian;
// language_resolve falls back to English for anything a disc does not have.
constexpr Language kLanguages[] = {
    {"en", kEnglish}, {"ja", kJapanese}, {"de", kGerman},  {"fr", kFrench},
    {"es", kSpanish}, {"it", kItalian},  {"ko", kKorean},  {"zh", kChinese},
};

int g_index = kEnglish;
std::string g_name = "en";

const char *name_of(int index)
{
    for (const Language &l : kLanguages)
        if (l.index == index) return l.name;
    return "en";
}

int value_of(const std::string &name)
{
    for (const Language &l : kLanguages)
        if (name == l.name) return l.index;
    return 0;
}

// The Windows user interface language as an XC_LANGUAGE value: BLACK follows the system on the PAL disc.
int from_system()
{
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_SPANISH: return kSpanish;
    case LANG_GERMAN: return kGerman;
    case LANG_FRENCH: return kFrench;
    case LANG_ITALIAN: return kItalian;
    case LANG_JAPANESE: return kJapanese;
    case LANG_KOREAN: return kKorean;
    case LANG_CHINESE: return kChinese;
    case LANG_ENGLISH: default: return kEnglish;
    }
}

// The PAL build's front-end keeps only its own languages (1 = MainUK, 3 German, 4 French, 5 Spanish, 6 Italian)
// and reads every other value as English; its NTSC counterpart reads 1 as MainUS.
bool build_reads(int index)
{
    if (!g_cfg.pal_build) return true;
    return index == kEnglish || index == kGerman || index == kFrench || index == kSpanish || index == kItalian;
}

// The text file the game builds for a language value, from its own table (0 UK, 1 US, 2 Ja, 3 Ge, 4 Fr, 5 Sp,
// 6 It, 7 Ko, 8/9 US) after the value's first mapping.
std::wstring strings_file(int index)
{
    static const wchar_t *kSuffix[10] = {L"UK", L"US", L"Ja", L"Ge", L"Fr", L"Sp", L"It", L"Ko", L"US", L"US"};
    int entry = index == kEnglish ? (g_cfg.pal_build ? 0 : 1) : (index >= 2 && index <= 9 ? index : 0);
    return std::wstring(L"Main") + kSuffix[entry] + L".bin";
}

bool have_strings(int index)
{
    if (g_cfg.game_dir.empty()) return false;
    std::wstring path = g_cfg.game_dir + L"\\language\\strings\\" + strings_file(index);
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

} // namespace

int language_index() { return g_index; }

const std::string &language_name() { return g_name; }

bool language_valid(const std::string &name) { return name == "auto" || value_of(name) != 0; }

std::wstring language_strings_file() { return strings_file(g_index); }

void language_resolve()
{
    if (!g_cfg.pal_build) {
        g_index = kEnglish; // the NTSC-U disc carries English only
        g_name = name_of(g_index);
        logf("language: en (the NTSC-U disc has English only)");
        return;
    }
    int index = g_cfg.language == "auto" ? from_system() : value_of(g_cfg.language);
    if (!index) index = kEnglish; // unknown name: English
    if (!build_reads(index)) {
        logf("language: this disc's front-end has no %s (it reads English there)", name_of(index));
        index = kEnglish;
    }
    if (!have_strings(index)) {
        logf("language: no %s text on this disc, using English", name_of(index));
        index = kEnglish;
    }
    g_index = index;
    g_name = name_of(index);
    logf("language: %s -> %s (XC_LANGUAGE %d), text %s", g_cfg.language.c_str(), g_name.c_str(), g_index,
         narrow(strings_file(g_index)).c_str());
}
