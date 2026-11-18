// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Resource packs, as Minecraft's: the resourcepacks folder beside the EXE, a pack per folder or .zip whose files, by
// the game's own paths (Matrix\..., sound\...), replace the game's pictures and sounds (Base::CFile::PackExtension:
// never the game's data, so a battle stays the same whatever packs its people have; nobody checks them).
// At its top (or in its only folder): pack.txt (UTF-8, '#' comments: Name=, NameEn=, Description=, DescriptionEn=,
// Author=) and pack.png, its picture in the menu; lang\Rus\ and lang\Eng\ hold what only one language's game takes
// (its voices), over the rest of the pack. A picture's _n and _s names (Name_n.png, Name_s.png) are kept for normal and
// specular maps (LabPBR) to come: the game takes them only for its own pictures of those names.
// The packs chosen (CFG\resourcepacks.txt) are used from the game's start: a zip unpacked into Cache\resourcepacks.
// A picture in a pack is the original's size times a power of two (PackPictures).
namespace ResourcePacks {

inline constexpr const wchar_t *folder = L"resourcepacks";
inline constexpr const wchar_t *choice_file = L"CFG\\resourcepacks.txt";
inline constexpr const wchar_t *cache_folder = L"Cache\\resourcepacks";
inline constexpr std::size_t max_icon = 1024 * 1024;              // pack.png
inline constexpr std::uint64_t max_file = 512ull * 1024 * 1024;   // a file in a zip

struct Pack {
    std::wstring id;  // its folder's or zip's name in resourcepacks (the .zip kept): what CFG\resourcepacks.txt names
    std::wstring name, name_en, description, description_en, author;
    std::filesystem::path place;  // the folder or the zip
    bool zip = false;
    std::wstring top;    // the folder inside it that is its top ('/' between folders, ending with '/'; empty: its own)
    std::string icon;    // pack.png's bytes (empty: none)
    std::wstring problem;  // why it cannot be used (empty: it can)
};

// pack.txt into a pack's texts (id: the name when it has none).
void parse(std::string_view text, Pack &pack);

// A zip's files: their paths inside ('/' between folders, as stored, the case kept), sizes and where they are.
struct ZipEntry {
    std::wstring name;
    std::uint32_t method = 0, packed = 0, size = 0, crc = 0, local = 0;
};
// The zip's list (stored or deflated files, not encrypted, not zip64); nullopt with `error`.
std::optional<std::vector<ZipEntry>> zip_entries(const std::filesystem::path &zip, std::string &error);
// One file of a zip (at most `limit` bytes); nullopt: not readable.
std::optional<std::string> zip_file(const std::filesystem::path &zip, const ZipEntry &entry, std::uint64_t limit);
// A path inside a pack made safe: '\' to '/', no "..", no drive or root; empty: not one.
std::wstring inside(std::wstring_view name);
// The top of a pack whose files are these (paths inside it): where pack.txt is, else the only folder when nothing
// lies beside it, else its own.
std::wstring top_of(const std::vector<std::wstring> &names);

// The packs in a folder by name.
std::vector<Pack> scan(const std::filesystem::path &packs);
// The resourcepacks folder's, read the first time; rescan reads it again.
const std::vector<Pack> &installed();
void rescan();
inline const Pack *find(const std::vector<Pack> &packs, std::wstring_view id) {
    for (const auto &pack : packs)
        if (pack.id == id) return &pack;
    return nullptr;
}

// The packs chosen, the first over the others (Pack=<id> lines).
using Choice = std::vector<std::wstring>;
Choice parse_choice(std::string_view text);
std::string choice_text(const Choice &choice);
Choice load_choice();
bool save_choice(const Choice &choice);

// A zip's files unpacked into `into` (a fresh folder; its stamp names the zip's size and time, so a zip unchanged is
// not unpacked again). false with `error`.
bool unpack(const std::filesystem::path &zip, const std::filesystem::path &into, std::string &error);
// The folders the game searches for a pack's files, the first first: its language's (lang\Rus or lang\Eng), its top.
std::vector<std::wstring> pack_roots(const std::filesystem::path &top, const wchar_t *language);

// The game's start: the packs chosen and installed (zips unpacked) as Base::CFile's pack roots; the packs used.
std::vector<std::wstring> start(const wchar_t *language);
// The packs the game started with (what the menu tells apart from a choice that needs a restart).
const Choice &started();

}  // namespace ResourcePacks
