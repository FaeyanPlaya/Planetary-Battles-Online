// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The files of Space Rangers HD that the standalone game needs (each person takes them from their own
// copy of the game; they are never shipped), and the fingerprints people of one battle must share.
namespace GameData {

// A folder holding Space Rangers HD: Rangers.exe and DATA\robots.pkg.
bool is_install(const std::filesystem::path &folder);

// Library folders listed in the text of Steam's libraryfolders.vdf ("path" entries, any drive).
std::vector<std::filesystem::path> steam_libraries(std::string_view vdf);

// Installs among these folders: each folder itself, and its subfolders (two levels) whose names
// mention Rangers. Each install once, in the order found.
std::vector<std::filesystem::path> installs_in(const std::vector<std::filesystem::path> &folders);

// Every install this PC has: all Steam libraries, GOG and uninstall entries in the registry, and the
// usual game folders (SteamLibrary, Games, GOG Games, Program Files, ...) on every fixed drive.
std::vector<std::filesystem::path> find_installs();

struct Copy {
    std::filesystem::path from, to;
    bool required = false;
};
// The files to copy from an install into the game folder; the voices and texts of the interface
// language first, the Russian robots.dat. Optional files the install lacks are left out.
std::vector<Copy> files_to_copy(const std::filesystem::path &install, const std::filesystem::path &game, bool russian);

// First start without DATA\robots.pkg: finds the game, asks (assume_yes: takes the first install
// found, for tests), copies. The install is only read. False when robots.pkg is still missing.
// The environment variable PBO_NO_SETUP turns it off (startup tests).
bool setup(const std::filesystem::path &game, bool assume_yes);

// CFG\game-path.txt in the game folder: the Space Rangers HD install the files came from (UTF-8), then the words
// "mods" once its Mods folder was seen there and "never" when the person said not to look for it again. An empty
// path: none was found.
struct Install {
    std::filesystem::path path;
    bool mods_seen = false, never = false;
    bool operator==(const Install &) const = default;
};
std::optional<Install> load_install(const std::filesystem::path &game);
void save_install(const std::filesystem::path &game, const Install &install);
// The Space Rangers HD Mods folder whose map mods the game offers (MapPacks): the saved install's. With none saved
// it looks for an install once, quietly. When the Mods folder seen there is gone it asks (ask: else nothing): look
// again, later (this start without), or never. Empty: none. PBO_NO_SETUP turns it off.
std::filesystem::path game_mods(const std::filesystem::path &game, bool ask);

// 64-bit FNV-1a of a file's bytes, continuing from seed; 0 when it cannot be read.
std::uint64_t hash_file(const std::filesystem::path &file, std::uint64_t seed = 14695981039346656037ull);
std::uint64_t hash_bytes(const void *data, std::size_t size, std::uint64_t seed = 14695981039346656037ull);
std::string hex(std::uint64_t value);

// The game files a battle reads (robots.pkg and the configuration in use) and this build's program:
// two people play together only with equal fingerprints. Computed once per process.
const std::string &data_fingerprint();
const std::string &build_fingerprint();

}  // namespace GameData
