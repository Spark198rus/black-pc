// The in-game PC SETTINGS pages, built at start from the user's own front-end file.
#pragma once

#include <string>

// Reads <game>\export\frontend\FEMain.bin, adds the pages and writes the result under out_root.
bool fe_build_pages(const std::wstring &game_dir, const std::wstring &out_root);

// Development helper: parses the game's front-end APT containers and writes
// human-readable character/sprite/control dumps under out_dir.
bool fe_dump_apt(const std::wstring &game_dir, const std::wstring &out_dir);