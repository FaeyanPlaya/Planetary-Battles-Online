// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <string>

// The name a person plays under: shown to the others in the lobby, in the battle's statistics (Tab) and
// in its notices. Kept in CFG\nickname.txt (UTF-8); the first start asks for one.
namespace PlayerName {

constexpr std::size_t min_length = 2;
constexpr std::size_t max_length = 16;

// Latin and Cyrillic letters, digits, '_' and '-': nothing that the lobby's lines or hint markup give
// a meaning, so a name travels and shows as it is.
bool valid_char(wchar_t ch);
bool valid(const std::wstring &name);
// Case-insensitive, Cyrillic too: two people may not share a name.
bool same(const std::wstring &a, const std::wstring &b);

std::wstring load();  // empty when none was chosen or the file is not a valid name
bool save(const std::wstring &name);

}  // namespace PlayerName
