// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>

// Optional original-game language data. This is the host's DAT format, not robots.dat's CStorage.
namespace MapMetadata {

struct Entry {
    std::wstring name;
    std::wstring description;
    std::wstring sides;
    int group = -1;
    int length = -1;
};

using Catalog = std::map<std::wstring, Entry>;  // Lowercase ASCII .cmap basename.

// Throws std::runtime_error on malformed, unsupported or oversized input. No partial catalog escapes.
Catalog decode(std::span<const uint8_t> bytes);

// Missing data is normal; other failures produce an error and an empty catalog.
Catalog load(std::string &error, const std::filesystem::path &path = L"CFG/Lang.dat");

// Plain display text: removes host formatting and prevents interpretation as hint markup.
std::wstring plain_text(std::wstring text);

}  // namespace MapMetadata
