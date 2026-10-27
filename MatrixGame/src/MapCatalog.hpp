// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <string>
#include <string_view>
#include <vector>

// Battle maps that the standalone game can offer: Matrix\Map\*.cmap in the registered packages and
// in the loose folder beside the EXE.
namespace MapCatalog {

inline constexpr const wchar_t *folder = L"Matrix\\Map";

struct Map {
    std::wstring name;     // File name without the extension, shown to the player.
    std::wstring path;     // Matrix\Map\<file>, the form that Config/Map uses.
    std::wstring preview;  // Matrix\Map\<image> with the same name, or empty.
    bool loose = false;    // A loose file, which the loader prefers to a packaged one.
    bool operator==(const Map &) const = default;
};

// Builds the list from the file names of the packaged and loose folders. Names compare
// case-insensitively and a loose map replaces a packaged one, as the loader does. A .jpg preview
// is preferred to a .png one. Only maps with valid remembered paths are listed, sorted by name.
std::vector<Map> merge(const std::vector<std::wstring> &packed, const std::vector<std::wstring> &loose);

// Lists the registered packages and the loose folder relative to the working directory.
std::vector<Map> scan();

// Index of the map with this path, compared case-insensitively, or -1.
int find(const std::vector<Map> &maps, std::wstring_view path);

// A relative path ending in .cmap without empty, current or parent components, made of ASCII letters,
// digits, spaces and the punctuation " _-.()+!#&,'~". The settings file stores ASCII values and gives
// '=', braces and "//" meaning, hint markup does so for '|' and brackets, and loose files are opened
// through narrow file names.
bool valid_path(std::wstring_view path);

// Whether the file loads as a map store with the properties table that map preparation reads first.
bool readable(const std::wstring &path);

}  // namespace MapCatalog
