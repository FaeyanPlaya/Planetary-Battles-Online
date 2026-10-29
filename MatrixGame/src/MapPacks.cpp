// MatrixGame - licensed under GPLv2 or any later version.
#include "MapPacks.hpp"

#include "Common.hpp"
#include "MapCatalog.hpp"
#include "GameData.hpp"
#include "MatrixGame.h"
#include "Mods.hpp"

#include "CBlockPar.hpp"
#include "CFile.hpp"
#include "CStorage.hpp"
#include "Cache.hpp"
#include "utils.hpp"

#include <stupid_logger.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <cstring>

namespace MapPacks {
namespace {

std::wstring folded(std::wstring_view text) {
    std::wstring out(text);
    for (wchar_t &ch : out) {
        if (ch >= L'A' && ch <= L'Z') ch = wchar_t(ch - L'A' + L'a');
        else if (ch == L'/') ch = L'\\';
    }
    return out;
}

bool picture(const std::filesystem::path &file) {
    const std::wstring ext = folded(file.extension().wstring());
    return ext == L".png" || ext == L".dds" || ext == L".jpg" || ext == L".bmp" || ext == L".tga";
}

std::optional<std::string> file_bytes(const std::filesystem::path &file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// Name= of ModuleInfo.txt (SR HD's) or mod.txt (ours), else empty.
std::wstring named(const std::filesystem::path &folder) {
    for (const wchar_t *file : {L"ModuleInfo.txt", L"mod.txt"}) {
        const auto bytes = file_bytes(folder / file);
        if (!bytes) continue;
        std::wstring_view text;
        const std::wstring decoded = Mods::decode(*bytes);
        text = decoded;
        while (!text.empty()) {
            const auto end = text.find(L'\n');
            const std::wstring line = utils::trim(std::wstring(text.substr(0, end)));
            text = end == std::wstring_view::npos ? std::wstring_view{} : text.substr(end + 1);
            const auto eq = line.find(L'=');
            if (eq != std::wstring::npos && folded(utils::trim(line.substr(0, eq))) == L"name") {
                const std::wstring value = utils::trim(line.substr(eq + 1));
                if (!value.empty()) return value;
            }
        }
    }
    return {};
}

bool has_maps(const std::filesystem::path &folder) {
    std::error_code error;
    for (std::filesystem::directory_iterator it(folder / maps_folder, error), end; !error && it != end; it.increment(error))
        if (it->is_regular_file(error) && folded(it->path().extension().wstring()) == L".cmap") return true;
    return false;
}

// A path into a mod's folder as SR HD names it (Mods\<section>\<mod>\rest): rest; any other path as it is.
std::wstring local(const std::wstring &path) {
    if (!folded(path).starts_with(L"mods\\")) return path;
    std::size_t at = 0;
    for (int part = 0; part < 3 && at != std::wstring::npos; ++part) {
        at = path.find_first_of(L"\\/", at);
        if (at != std::wstring::npos) ++at;
    }
    return at == std::wstring::npos ? path : path.substr(at);
}

// A sky side's value: the texture, then its rectangle after commas.
std::wstring texture_of(const std::wstring &value) { return value.substr(0, value.find(L',')); }

}  // namespace

std::vector<Pack> scan(const std::filesystem::path &mods, bool sections) {
    std::vector<Pack> packs;
    std::vector<std::filesystem::path> folders;
    std::error_code error;
    for (std::filesystem::directory_iterator it(mods, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_directory(error)) continue;
        if (!sections) {
            folders.push_back(it->path());
            continue;
        }
        std::error_code inner;
        for (std::filesystem::directory_iterator mod(it->path(), inner), none; !inner && mod != none; mod.increment(inner))
            if (mod->is_directory(inner)) folders.push_back(mod->path());
    }
    for (const auto &folder : folders) {
        const std::wstring leaf = folder.filename().wstring();
        const std::string id = Mods::id_of(leaf);
        if (id.empty() || find(packs, id) || !has_maps(folder)) continue;
        Pack pack{id, named(folder), folder.wstring(), std::wstring(Mods::folder) + L"\\" + leaf};
        if (pack.name.empty()) pack.name = leaf;
        packs.push_back(std::move(pack));
    }
    std::sort(packs.begin(), packs.end(), [](const Pack &a, const Pack &b) { return a.id < b.id; });
    return packs;
}

namespace {
std::filesystem::path g_game_mods;
}

void set_game_mods(const std::filesystem::path &mods) { g_game_mods = mods; }

const std::vector<Pack> &installed() {
    static const std::vector<Pack> packs = [] {
        std::vector<Pack> all = scan(Mods::folder);
        if (!g_game_mods.empty())
            for (Pack &pack : scan(g_game_mods, true)) {
                if (find(all, pack.id)) continue;  // the copy beside the EXE wins
                // Its maps are read under their mods\<folder> names.
                for (const std::wstring &file : map_files(pack))
                    Base::CFile::Redirect(pack.path + L"\\" + maps_folder + L"\\" + file,
                                          (std::filesystem::path(pack.folder) / maps_folder / file).wstring());
                all.push_back(std::move(pack));
            }
        std::sort(all.begin(), all.end(), [](const Pack &a, const Pack &b) { return a.id < b.id; });
        return all;
    }();
    return packs;
}

const Pack *find(const std::vector<Pack> &packs, std::string_view id) {
    const auto it = std::find_if(packs.begin(), packs.end(), [&](const Pack &p) { return p.id == id; });
    return it == packs.end() ? nullptr : &*it;
}

const Pack *of_map(const std::vector<Pack> &packs, std::wstring_view map_path) {
    const std::wstring path = folded(map_path);
    for (const Pack &pack : packs) {
        const std::wstring prefix = folded(pack.path) + L"\\" + folded(maps_folder) + L"\\";
        if (path.starts_with(prefix) && path.find(L'\\', prefix.size()) == std::wstring::npos) return &pack;
    }
    return nullptr;
}

std::vector<std::wstring> map_files(const Pack &pack) {
    std::vector<std::wstring> files;
    std::error_code error;
    for (std::filesystem::directory_iterator it(std::filesystem::path(pack.folder) / maps_folder, error), end;
         !error && it != end; it.increment(error))
        if (it->is_regular_file(error)) files.push_back(it->path().filename().wstring());
    return files;
}

std::string fingerprint(const Pack &pack) {
    static std::map<std::wstring, std::string> known;
    if (const auto found = known.find(pack.folder); found != known.end()) return found->second;
    std::vector<std::pair<std::string, std::filesystem::path>> files;
    std::error_code error;
    const std::filesystem::path root(pack.folder);
    for (auto it = std::filesystem::recursive_directory_iterator(root, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        files.emplace_back(utils::from_wstring(folded(std::filesystem::relative(it->path(), root, error).wstring())), it->path());
    }
    std::sort(files.begin(), files.end());
    std::uint64_t h = GameData::hash_bytes("pbo-maps", 8);
    for (const auto &[name, path] : files) {
        const std::string size = std::to_string(std::filesystem::file_size(path, error));
        h = GameData::hash_bytes(name.data(), name.size() + 1, h);  // with the terminating zero
        h = GameData::hash_bytes(size.data(), size.size() + 1, h);
        if (picture(path)) continue;
        const auto bytes = file_bytes(path);
        h = bytes ? GameData::hash_bytes(bytes->data(), bytes->size(), h) : GameData::hash_bytes("?", 1, h);
    }
    return known[pack.folder] = GameData::hex(h);
}

std::string ref(const Pack &pack) { return pack.id + ":" + fingerprint(pack); }

std::filesystem::path names_file(const Pack &pack, bool russian) {
    const std::filesystem::path root(pack.folder);
    const wchar_t *const languages[] = {russian ? L"Rus" : L"Eng", russian ? L"Eng" : L"Rus"};
    for (const wchar_t *language : languages) {
        const auto file = root / L"CFG" / language / L"Lang.dat";
        std::error_code error;
        if (std::filesystem::is_regular_file(file, error)) return file;
    }
    return {};
}

void merge_textures(Base::CBlockPar &textures, Base::CBlockPar &data,
                    const std::function<bool(const std::wstring &)> &exists) {
    if (Base::CBlockPar *skies = textures.BlockGetNE(L"Sky")) {
        for (int i = 0; i < skies->BlockCount(); ++i) {
            Base::CBlockPar &from = *skies->BlockGet(i);
            Base::CBlockPar &to = *data.BlockGetAdd(L"Sky")->BlockGetAdd(skies->BlockGetName(i));
            for (const auto &[source, target] : {std::pair{L"Front", L"Fore"}, {L"Fore", L"Fore"}, {L"Back", L"Back"},
                                                 {L"Left", L"Left"}, {L"Right", L"Right"}})
                if (from.ParCount(source)) to.ParSetAdd(target, local(utils::trim(from.ParGet(source))));
            std::wstring reflection = from.ParCount(L"Reflection") ? local(utils::trim(from.ParGet(L"Reflection"))) : L"";
            if (reflection.empty() || !exists(reflection)) reflection = L"Matrix\\Textures\\reflection";
            to.ParSetAdd(L"Reflection", reflection);
            for (const wchar_t *angle : {L"Angle", L"DeltaAngle"})
                to.ParSetAdd(angle, from.ParCount(angle) ? utils::trim(from.ParGet(angle)) : std::wstring(L"0"));
        }
    }
    if (Base::CBlockPar *waters = textures.BlockGetNE(L"Water")) {
        for (int i = 0; i < waters->BlockCount(); ++i) {
            Base::CBlockPar &from = *waters->BlockGet(i);
            Base::CBlockPar &to = *data.BlockGetAdd(L"Water")->BlockGetAdd(waters->BlockGetName(i));
            for (const auto &[source, target] : {std::pair{L"Water", L"water"}, {L"Reflection", L"mirror"},
                                                 {L"InshoreWave", L"inshore"}})
                if (from.ParCount(source)) to.ParSetAdd(target, local(utils::trim(from.ParGet(source))));
        }
    }
}

namespace {
std::vector<Pack> g_received;
}

void receive(const std::wstring &map_path, const std::wstring &folder) {
    // The map is <path>\DATA\Maps\<file>: its pack's path is what comes before.
    const std::wstring folded_map = folded(map_path);
    const auto at = folded_map.rfind(L"\\" + folded(maps_folder) + L"\\");
    if (at == std::wstring::npos) return;
    const std::wstring path = map_path.substr(0, at);
    const std::string id = Mods::id_of(std::filesystem::path(path).filename().wstring());
    if (id.empty()) return;
    std::erase_if(g_received, [&](const Pack &p) { return folded(p.path) == folded(path); });
    g_received.push_back({id, std::filesystem::path(path).filename().wstring(), folder, path});
}

void forget_received() { g_received.clear(); }

const Pack *for_map(std::wstring_view map_path) {
    if (const Pack *pack = of_map(g_received, map_path)) return pack;
    return of_map(installed(), map_path);
}

const Pack *use_for(std::wstring_view map_path) {
    const Pack *pack = for_map(map_path);
    if (!pack) {
        Base::CFile::SetSearchRoots({});
        return nullptr;
    }
    Base::CFile::SetSearchRoots({pack->folder});
    const std::filesystem::path root(pack->folder);
    for (const wchar_t *language : {L"Rus", L"Eng"}) {
        const auto cache = root / L"CFG" / language / L"Robots" / L"cache.txt";
        std::error_code error;
        if (!std::filesystem::is_regular_file(cache, error)) continue;
        try {
            Base::CBlockPar bp;
            bp.LoadFromTextFile(cache.wstring());
            Base::CBlockPar *map = bp.BlockGetNE(L"Map");
            Base::CBlockPar *textures = map ? map->BlockGetNE(L"Textures") : nullptr;
            if (textures)
                merge_textures(*textures, *g_MatrixData, [](const std::wstring &name) {
                    std::wstring file;
                    return Base::CFile::FileExist(file, texture_of(name).c_str(), CacheExtsTex);
                });
        }
        catch (...) {
            lgr.warning("Map mod {}: its cache.txt could not be read; the maps keep the game's sky and water")(pack->id);
        }
        break;
    }
    lgr.notice("Map mod {} ({}) for this battle")(pack->id, utils::from_wstring(pack->folder));
    return pack;
}

std::vector<Resource> required(const std::vector<std::wstring> &strings, bool with_gloss) {
    std::vector<Resource> out;
    const auto add = [&](std::wstring name, bool model) {
        name = utils::trim(name.substr(0, name.find(L'?')));
        if (!name.empty() && std::find(out.begin(), out.end(), Resource{name, model}) == out.end())
            out.push_back({std::move(name), model});
    };
    for (const std::wstring &s : strings) {
        if (!folded(s).starts_with(L"matrix\\")) continue;
        if (s.find(L'*') == std::wstring::npos) {  // the ground: a texture (?gloss=... is optional)
            add(s, false);
            const auto gloss = folded(s).find(L"?gloss=");
            if (with_gloss && gloss != std::wstring::npos) {
                const std::wstring name = s.substr(gloss + 7, s.find(L'?', gloss + 1) - gloss - 7);
                const auto slash = s.find_last_of(L"\\/", gloss);
                if (!name.empty() && slash != std::wstring::npos) add(s.substr(0, slash + 1) + name, false);
            }
            continue;
        }
        // An object: path*model*texture*gloss*back*mask*scroll*... (MatrixObject's OTP_ fields).
        std::vector<std::wstring> parts;
        for (std::size_t at = 0;;) {
            const std::size_t star = s.find(L'*', at);
            parts.push_back(s.substr(at, star == std::wstring::npos ? std::wstring::npos : star - at));
            if (star == std::wstring::npos) break;
            at = star + 1;
        }
        const std::wstring &path = parts[0];
        if (parts.size() > 1 && !parts[1].empty()) add(path + parts[1] + L".vo", true);
        for (std::size_t field = 2; field <= 5 && field < parts.size(); ++field)
            if (!parts[field].empty() && parts[field].front() != L'?') add(path + parts[field], false);
    }
    return out;
}

std::vector<std::wstring> missing(const std::wstring &map_path, std::size_t limit) {
    std::vector<std::wstring> strings;
    try {
        Base::CStorage storage;
        if (!storage.Load(map_path.c_str())) return {map_path};
        Base::CDataBuf *buf = storage.GetBuf(DATA_STRINGS, DATA_STRINGS_STRING, Base::ST_WCHAR);
        if (!buf) return {map_path};
        for (DWORD i = 0; i < buf->GetArraysCount(); ++i) strings.push_back(buf->GetAsWStr(int(i)));
    }
    catch (...) {
        return {map_path};
    }
    const Pack *pack = for_map(map_path);
    const auto roots = Base::CFile::SearchRoots();
    Base::CFile::SetSearchRoots(pack ? std::vector<std::wstring>{pack->folder} : std::vector<std::wstring>{});
    std::vector<std::wstring> lacking;
    for (const Resource &r : required(strings)) {
        std::wstring file;
        if (Base::CFile::FileExist(file, r.name.c_str(), r.model ? nullptr : CacheExtsTex)) continue;
        lacking.push_back(r.name);
        if (lacking.size() >= limit) break;
    }
    Base::CFile::SetSearchRoots(roots);
    return lacking;
}

namespace {
bool g_check = false;
constexpr std::string_view bundle_magic = "PBOMAPS1";

// The map's strings and its properties (name -> value).
bool map_tables(const std::wstring &map_path, std::vector<std::wstring> &strings, std::map<std::wstring, std::wstring> &properties) {
    try {
        Base::CStorage storage;
        if (!storage.Load(map_path.c_str())) return false;
        if (Base::CDataBuf *buf = storage.GetBuf(DATA_STRINGS, DATA_STRINGS_STRING, Base::ST_WCHAR))
            for (DWORD i = 0; i < buf->GetArraysCount(); ++i) strings.push_back(buf->GetAsWStr(int(i)));
        Base::CDataBuf *names = storage.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_NAME, Base::ST_WCHAR);
        Base::CDataBuf *values = storage.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_VALUE, Base::ST_WCHAR);
        if (names && values)
            for (DWORD i = 0; i < names->GetArraysCount() && i < values->GetArraysCount(); ++i)
                properties[names->GetAsWStr(int(i))] = values->GetAsWStr(int(i));
        return true;
    }
    catch (...) {
        return false;
    }
}

void put_u32(std::string &out, std::uint32_t v) { out.append(reinterpret_cast<const char *>(&v), 4); }
void put_u64(std::string &out, std::uint64_t v) { out.append(reinterpret_cast<const char *>(&v), 8); }

// A path a bundle may write: relative, below the folder, of plain characters.
bool safe(std::wstring_view path) {
    if (path.empty() || path.size() > 260 || path.front() == L'\\' || path.front() == L'/' || path.find(L':') != std::wstring_view::npos)
        return false;
    for (wchar_t ch : path)
        if (ch < 32 || std::wstring_view(L"<>\"|?*").find(ch) != std::wstring_view::npos) return false;
    for (std::size_t begin = 0; begin <= path.size();) {
        std::size_t end = path.find_first_of(L"\\/", begin);
        if (end == std::wstring_view::npos) end = path.size();
        const auto part = path.substr(begin, end - begin);
        if (part.empty() || part == L"." || part == L"..") return false;
        begin = end + 1;
    }
    return true;
}
}  // namespace

std::vector<std::wstring> bundle_files(const std::wstring &map_path) {
    const Pack *pack = of_map(installed(), map_path);
    if (!pack) return {};
    std::vector<std::wstring> strings;
    std::map<std::wstring, std::wstring> properties;
    if (!map_tables(map_path, strings, properties)) return {};
    std::vector<std::wstring> files{std::wstring(maps_folder) + L"\\" + std::filesystem::path(map_path).filename().wstring()};
    const auto add = [&](std::wstring file) {
        if (std::find_if(files.begin(), files.end(), [&](const std::wstring &f) { return folded(f) == folded(file); }) == files.end())
            files.push_back(std::move(file));
    };
    const std::filesystem::path root(pack->folder);
    for (const wchar_t *file : {L"CFG\\Rus\\Robots\\cache.txt", L"CFG\\Eng\\Robots\\cache.txt", L"CFG\\Rus\\Lang.dat", L"CFG\\Eng\\Lang.dat"}) {
        std::error_code error;
        if (std::filesystem::is_regular_file(root / file, error)) add(file);
    }
    // Every resource as the loader finds it with the pack searched first: those in the pack's folder go.
    std::vector<Resource> wanted = required(strings, true);
    for (const wchar_t *language : {L"Rus", L"Eng"}) {
        std::error_code error;
        const auto cache = root / L"CFG" / language / L"Robots" / L"cache.txt";
        if (!std::filesystem::is_regular_file(cache, error)) continue;
        try {
            Base::CBlockPar bp;
            bp.LoadFromTextFile(cache.wstring());
            Base::CBlockPar *map = bp.BlockGetNE(L"Map");
            Base::CBlockPar *textures = map ? map->BlockGetNE(L"Textures") : nullptr;
            const auto entry = [&](const wchar_t *kind, const wchar_t *property) -> Base::CBlockPar * {
                Base::CBlockPar *block = textures ? textures->BlockGetNE(kind) : nullptr;
                const auto name = properties.find(property);
                return block && name != properties.end() ? block->BlockGetNE(name->second) : nullptr;
            };
            if (Base::CBlockPar *sky = entry(L"Sky", DATA_SKYNAME))
                for (const wchar_t *side : {L"Front", L"Fore", L"Back", L"Left", L"Right", L"Reflection"})
                    if (sky->ParCount(side)) {
                        const std::wstring texture = texture_of(local(utils::trim(sky->ParGet(side))));
                        wanted.push_back({texture, false});
                        wanted.push_back({texture + L"_high", false});
                    }
            if (Base::CBlockPar *water = entry(L"Water", DATA_WATERNAME))
                for (const wchar_t *field : {L"Water", L"Reflection", L"InshoreWave"})
                    if (water->ParCount(field)) wanted.push_back({local(utils::trim(water->ParGet(field))), false});
        }
        catch (...) {
        }
        break;
    }
    const auto roots = Base::CFile::SearchRoots();
    Base::CFile::SetSearchRoots({pack->folder});
    const std::wstring prefix = folded(pack->folder) + L"\\";
    for (const Resource &r : wanted) {
        std::wstring file;
        if (Base::CFile::FileExist(file, r.name.c_str(), r.model ? nullptr : CacheExtsTex) && folded(file).starts_with(prefix))
            add(file.substr(prefix.size()));
    }
    Base::CFile::SetSearchRoots(roots);
    return files;
}

std::optional<std::string> bundle(const std::wstring &map_path) {
    static std::wstring last_path;
    static std::string last;
    if (!last_path.empty() && folded(last_path) == folded(map_path)) return last;
    const Pack *pack = of_map(installed(), map_path);
    if (!pack) return std::nullopt;
    std::string out(bundle_magic);
    for (const std::wstring &file : bundle_files(map_path)) {
        const auto bytes = file_bytes(std::filesystem::path(pack->folder) / file);
        if (!bytes) return std::nullopt;
        const std::string name = utils::from_wstring(file);
        put_u32(out, std::uint32_t(name.size()));
        out += name;
        put_u64(out, bytes->size());
        out += *bytes;
    }
    last_path = map_path;
    last = out;
    return out;
}

bool is_bundle(std::string_view bytes) { return bytes.starts_with(bundle_magic); }

bool unpack(std::string_view bytes, const std::filesystem::path &folder) {
    if (!is_bundle(bytes)) return false;
    std::size_t at = bundle_magic.size();
    const auto take = [&](std::size_t n) -> std::optional<std::string_view> {
        if (bytes.size() - at < n) return std::nullopt;
        const auto part = bytes.substr(at, n);
        at += n;
        return part;
    };
    while (at < bytes.size()) {
        std::uint32_t length = 0;
        std::uint64_t size = 0;
        const auto l = take(4);
        if (!l) return false;
        std::memcpy(&length, l->data(), 4);
        const auto name = take(length);
        const auto s = name ? take(8) : std::nullopt;
        if (!s) return false;
        std::memcpy(&size, s->data(), 8);
        const auto data = take(std::size_t(size));
        if (!data || size > bytes.size()) return false;
        std::wstring path;
        try {
            path = utils::to_wstring(*name);
        }
        catch (...) {
            return false;
        }
        if (!safe(path)) return false;
        const auto file = folder / path;
        std::error_code error;
        std::filesystem::create_directories(file.parent_path(), error);
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(data->data(), std::streamsize(data->size()));
        if (!out) return false;
    }
    return true;
}

void request_check() { g_check = true; }
bool check_requested() { return g_check; }

void log_missing() {
    auto maps = MapCatalog::scan();
    for (const Pack &pack : installed()) {
        auto more = MapCatalog::listed(pack.path + L"\\" + maps_folder, map_files(pack));
        maps.insert(maps.end(), more.begin(), more.end());
    }
    int lacking = 0;
    std::size_t largest = 0;
    for (const auto &map : maps) {
        if (!map.pack.empty() || of_map(installed(), map.path)) {
            const auto b = bundle(map.path);
            const std::size_t size = b ? b->size() : 0;
            largest = std::max(largest, size);
            lgr.notice("Map check: {} bundle {} files, {} bytes")(utils::from_wstring(map.path), bundle_files(map.path).size(), size);
        }
        const auto files = missing(map.path, 1000);
        if (files.empty()) continue;
        ++lacking;
        std::string list;
        for (const auto &f : files) list += (list.empty() ? "" : ", ") + utils::from_wstring(f);
        lgr.warning("Map check: {} lacks {} file(s): {}")(utils::from_wstring(map.path), files.size(), list);
    }
    lgr.notice("Map check: {} maps, {} lack files; {} map mods, the largest bundle {} bytes")(maps.size(), lacking,
                                                                                            installed().size(), largest);
}

}  // namespace MapPacks
