// Values the port shows in, and gives to, the front end's pages (fe/vars.cpp).
#pragma once

#include <cstdint>
#include <string>

// The pages show the same few strings, filled for whichever page is up: row i of a page shows &PC_V<i> (its value,
// or a slider's number on the main menu), &PC_V<6+i> (a caption the port supplies: key bindings, the pause page) and
// &PC_V<12+i> (the pause page's slider number); &PC_V18 is the pause page's title. &PC_V19 / &PC_V20 name the keys
// for CONFIRM / BACK in the menus' control legend while the keyboard is in use (input/prompts.cpp).
namespace fevar {
constexpr int kRows = 6, kValue = 0, kLabel = 6, kNumber = 12, kTitle = 18, kKeyConfirm = 19, kKeyBack = 20;
}
constexpr int kFeVars = 21;

void fe_vars_poll();                              // once per front-end update: keeps the values registered
void fe_var_set(int n, const std::wstring &text); // shown at once by every text field bound to &PC_V<n>
void fe_int_set(const char *name, int value);     // externObject.<name> for the pages' scripts (name starts with "FE")
int fe_int_get(const char *name);                 // its value now (the scripts can write it too); 0 if never set
uint32_t fe_hash(const char *name);               // the front end's name hash (FUN_000BF2A0)
