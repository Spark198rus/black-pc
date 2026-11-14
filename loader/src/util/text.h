// UTF-8 <-> UTF-16.
#pragma once

#include <string>

std::string narrow(const std::wstring &w);
std::wstring widen(const std::string &s);
