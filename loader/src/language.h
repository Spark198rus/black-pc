// The language the game speaks and shows its loading video in. The PAL disc ships several languages (text in
// language\strings\MainXX.bin and the loading video in videos\LO_p_XX.xmv) chosen from the console's XC_LANGUAGE
// setting; the port answers that setting with the Windows user interface language, so BLACK follows the system.
// The NTSC-U disc has English only: that build always stays on English.
#pragma once

#include <string>

// The Xbox XC_LANGUAGE value the game is told (1 English, 2 Japanese, 3 German, 4 French, 5 Spanish, 6 Italian,
// 7 Korean, 8/9 Chinese). English until language_resolve, and always English for the NTSC-U build.
int language_index();

// Short name of the resolved language ("auto", "en", "es", ...), for logs and the ini.
const std::string &language_name();

// "auto" (the Windows user interface language) or one of the short names the port knows.
bool language_valid(const std::string &name);

// The text file the game will read for the resolved language ("MainSp.bin", ...), following the game's own
// table and its build-specific English file (MainUK on PAL discs, MainUS on the NTSC disc).
std::wstring language_strings_file();

// Resolve g_cfg.language for the build that was loaded. Call once the XBE is identified (needs g_cfg.pal_build
// and g_cfg.game_dir): a language the disc does not carry, or one the PAL build reads as English, becomes English.
void language_resolve();

// g_cfg.language as it was when language_resolve ran: the setting this run uses (a PC SETTINGS change applies at the
// next start).
const std::string &language_at_start();
