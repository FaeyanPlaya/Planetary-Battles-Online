// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Base { class CBlockPar; }

// Map mods: Space Rangers HD's planetary battle map mods (folders with battle maps in DATA\Maps, PBNewMaps): those in
// its own Mods folder (Mods\<section>\<mod>, read in place) and those in mods beside the EXE (copied as they are; one
// there replaces the game's of its name). Their maps join the list, named mods\<mod>\DATA\Maps\<file> wherever the
// mod lies (CFile redirects those of the game's folder). A battle on one of them finds the mod's loose files (its
// Matrix\...) before the game's, and knows the sky and water its CFG\Rus|Eng\Robots\cache.txt names (Map.Textures).
// Everyone in a battle needs the same mod: the lobby compares fingerprints, and a host gives a client without it (or
// with another) the map with the mod's files it takes (bundle), which the client unpacks into NetMaps (MapShare).
namespace MapPacks {

struct Pack {
    std::string id;       // the folder's name in lower case (Mods::id_of)
    std::wstring name;    // ModuleInfo.txt's Name (or mod.txt's), else the folder's name
    std::wstring folder;  // where it lies: mods\<folder> beside the EXE, or in the game's Mods
    std::wstring path;    // mods\<folder>: how its maps are named (DATA\Maps under it)
    bool operator==(const Pack &) const = default;
};

inline constexpr const wchar_t *maps_folder = L"DATA\\Maps";

// The folders of `mods` with a .cmap in DATA\Maps, by id; a folder whose name is no id is left out. sections: the
// game's Mods, whose mods lie a folder deeper (Mods\<section>\<mod>).
std::vector<Pack> scan(const std::filesystem::path &mods, bool sections = false);
// Where the game's Mods folder is (GameData::game_mods); before installed() is first called. Empty: none.
void set_game_mods(const std::filesystem::path &mods);
// The mods folder beside the EXE and the game's, scanned once; the maps of the game's are redirected.
const std::vector<Pack> &installed();
const Pack *find(const std::vector<Pack> &packs, std::string_view id);
// The pack whose DATA\Maps holds this map (<path>\DATA\Maps\<file>.cmap, case aside), or nullptr.
const Pack *of_map(const std::vector<Pack> &packs, std::wstring_view map_path);
// The file names in the pack's DATA\Maps.
std::vector<std::wstring> map_files(const Pack &pack);
// File names and sizes, and the bytes of every file but pictures (.png .dds .jpg .bmp .tga: they change only the
// look); read once per pack.
std::string fingerprint(const Pack &pack);
// "<id>:<fingerprint>", the lobby's mark of a map from this pack.
std::string ref(const Pack &pack);
// The Lang.dat with the pack's map names (CFG\Rus or CFG\Eng, else the other), or empty.
std::filesystem::path names_file(const Pack &pack, bool russian);

// SR HD's cache.txt Map.Textures into the engine's Sky and Water blocks of `data`: a sky's Front becomes Fore, Back,
// Left, Right, Reflection, Angle and DeltaAngle stay; a water's Water, Reflection and InshoreWave become water, mirror
// and inshore. A path into a mod's folder (Mods\<section>\<mod>\rest) becomes rest, which the pack's folder (searched
// first) or the game holds; a reflection `exists` does not find becomes the game's own.
void merge_textures(Base::CBlockPar &textures, Base::CBlockPar &data,
                    const std::function<bool(const std::wstring &)> &exists);

// A battle on this map: its pack's files are found first and its sky and water are known; a map of no pack clears
// that. Returns the pack, or nullptr.
const Pack *use_for(std::wstring_view map_path);
// The pack a battle on this map reads: one received for it, else an installed one; nullptr: the game's map.
const Pack *for_map(std::wstring_view map_path);
// The files a host's bundle of this map holds from a host's own map mod, received into folder (NetMaps\<hash>):
// found before the installed mods' for that map, until forget_received.
void receive(const std::wstring &map_path, const std::wstring &folder);
void forget_received();

// The files of its pack a map takes, as paths in the pack: DATA\Maps\<map>, the textures and models it names that
// the pack's folder holds (a gloss too), its sky's and water's, cache.txt and Lang.dat. Empty: not a map mod's map.
std::vector<std::wstring> bundle_files(const std::wstring &map_path);
// "PBOMAPS1", then each of bundle_files: u32 length and its path (UTF-8), u64 size and its bytes; read once per map
// (the last one kept). nullopt: not a map mod's map.
std::optional<std::string> bundle(const std::wstring &map_path);
bool is_bundle(std::string_view bytes);
// Writes a bundle's files into folder; false (and maybe some written) when it is broken or names a path outside it.
bool unpack(std::string_view bytes, const std::filesystem::path &folder);

// The resources a map's strings name that the loader must find: an object's model (.vo) and textures, the ground's
// textures (a gloss is optional: the loader skips one it does not find).
struct Resource {
    std::wstring name;
    bool model = false;  // a .vo, else a texture
    bool operator==(const Resource &) const = default;
};
std::vector<Resource> required(const std::vector<std::wstring> &strings, bool with_gloss = false);
// The files this map names that the loader would not find (its pack's folder searched first), at most `limit`
// of them; empty: it loads. An unreadable map gives its own path.
std::vector<std::wstring> missing(const std::wstring &map_path, std::size_t limit = 3);
// --net-check-maps: the game logs what every map of the list (the game's and the map mods') lacks, then quits.
void request_check();
bool check_requested();
void log_missing();

}  // namespace MapPacks
