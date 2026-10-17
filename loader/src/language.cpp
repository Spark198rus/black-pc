#include "language.h"

#include <array>
#include <string_view>

namespace language {
namespace {
struct Locale { const char *id; const wchar_t *name; };
constexpr std::array<Locale, 6> kLocales = {{{"en", L"ENGLISH"}, {"pt-BR", L"PORTUGUÊS (BRASIL)"},
                                             {"es", L"ESPAÑOL"}, {"fr", L"FRANÇAIS"},
                                             {"de", L"DEUTSCH"}, {"it", L"ITALIANO"}}};
constexpr const char *kTitles[6][5] = {
    {"VIDEO", "DISPLAY", "AUDIO AND GAME", "CONTROLS", "LANGUAGES"},
    {"VÍDEO", "TELA", "ÁUDIO E JOGO", "CONTROLES", "IDIOMAS"},
    {"VÍDEO", "PANTALLA", "AUDIO Y JUEGO", "CONTROLES", "IDIOMAS"},
    {"VIDÉO", "AFFICHAGE", "AUDIO ET JEU", "COMMANDES", "LANGUES"},
    {"VIDEO", "ANZEIGE", "AUDIO UND SPIEL", "STEUERUNG", "SPRACHEN"},
    {"VIDEO", "SCHERMO", "AUDIO E GIOCO", "CONTROLLI", "LINGUE"},
};
constexpr const wchar_t *kLanguageLabels[] = {L"LANGUAGE", L"IDIOMA DO JOGO", L"IDIOMA", L"LANGUE", L"SPRACHE", L"LINGUA"};
constexpr const wchar_t *kSettingsTitles[] = {L"PC SETTINGS", L"CONFIGURAÇÕES DO PC", L"AJUSTES DE PC", L"PARAMÈTRES PC", L"PC-EINSTELLUNGEN", L"IMPOSTAZIONI PC"};
constexpr const char *kLanguageCaptions[] = {"LANGUAGE", "IDIOMA DO JOGO", "IDIOMA", "LANGUE", "SPRACHE", "LINGUA"};
}

int locale_count() { return (int)kLocales.size(); }
const char *locale_id(int index) { return index >= 0 && index < locale_count() ? kLocales[index].id : kLocales[0].id; }
const wchar_t *display_name(int index) { return index >= 0 && index < locale_count() ? kLocales[index].name : kLocales[0].name; }
bool is_supported(const std::string &id)
{
    for (const Locale &locale : kLocales) if (id == locale.id) return true;
    return false;
}
std::string normalize(const std::string &id) { return is_supported(id) ? id : "en"; }
int locale_index(const std::string &id)
{
    for (int i = 0; i < locale_count(); i++) if (id == kLocales[i].id) return i;
    return 0;
}
const char *group_title(int group, const std::string &locale)
{
    if (group < 0 || group >= 5) return "";
    return kTitles[locale_index(locale)][group];
}
const char *language_row_caption(const std::string &locale) { return kLanguageCaptions[locale_index(locale)]; }
const wchar_t *language_row_label(const std::string &locale) { return kLanguageLabels[locale_index(locale)]; }
const wchar_t *pc_settings_title(const std::string &locale) { return kSettingsTitles[locale_index(locale)]; }

} // namespace language
