// MatrixGame - licensed under GPLv2 or any later version.
#include "LobbyWindow.hpp"
#include "GameLanguage.hpp"
#include "LobbyModel.hpp"
#include "NetLobby.hpp"
#include "Lockstep.hpp"
#include "MapCatalog.hpp"
#include "MapShare.hpp"
#include "MapMetadata.hpp"
#include "MapPreview.hpp"
#include "MapSides.hpp"
#include "Mods.hpp"
#include "ModInfoWindow.hpp"
#include "GameArt.hpp"
#include "MapPacks.hpp"
#include "PlayerName.hpp"
#include "GameData.hpp"
#include "CBuf.hpp"
#include "BattleLoading.hpp"
#include "MenuKeyLatch.hpp"
#include "StringConstants.hpp"
#include "MatrixGame.h"
#include "input.hpp"
#include "WindowDisplay.hpp"
#include "Text/Font.hpp"
#include "CBitmap.hpp"
#include "CFile.hpp"

#include "Interface/MatrixHint.hpp"
#include "CBlockPar.hpp"
#include "3g.hpp"
#include "utils.hpp"

#include <stupid_logger.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <sstream>
#include <memory>
#include <optional>

namespace LobbyWindow {
namespace {

using LobbyModel::Language;
using LobbyModel::JoinError;

constexpr const wchar_t *address_file = L"CFG\\net-join.txt";  // the last host joined
constexpr const wchar_t *sort_file = L"CFG\\lobby-sort.txt";    // the map list's order: name or people
constexpr const wchar_t *hashes_file = L"CFG\\map-hashes.txt";  // the maps' fingerprints kept (map_hash)
constexpr const wchar_t *sides_file =L"CFG\\map-sides.txt";   // the maps' sides read before (the game restarts after a battle)
constexpr int connect_timeout_ms = 8000;
constexpr auto map_read_budget = std::chrono::milliseconds(15);

enum class Mode { Join, Host, Client, Name };

struct State {
    Mode mode = Mode::Join;
    std::chrono::steady_clock::time_point last_takt{};  // the slow-frame note's
    StandaloneSettings::Values values;
    NetBattle::Options options;
    Language language = Language::English;
    std::vector<MapCatalog::Map> maps;
    std::vector<std::wstring> names;
    std::vector<int> sides;  // playable sides of each map; -1 not read yet
    std::size_t next_read = 0;
    NetLobby::Host host;
    NetLobby::Client client;
    BattleChoice::Bonus bonus = BattleChoice::Bonus::None;
    int page = 0;
    std::wstring typed;
    JoinError error = JoinError::None;
    std::wstring host_address;
    std::string listen_address;  // the host's lobby
    int listen_port = Lobby::default_port;
    std::wstring name;          // Mode::Name: the name being typed
    bool name_forced = false;
    bool display_changed = false;  // the lobby allowed no exclusive mode: shutdown gives it back
    bool exclusive_was = true;  // the display's exclusive mode before the lobby
    std::wstring auto_ready_map;  // --lobby-ready: the map it was ready for once (a person may then say no)
    bool side_asked = false;      // --lobby-side asked for
    int kick_asked = -1;          // the host's: the person whose row asks whether to send them away
    std::string map_failed;       // a client's: the host's map file (fingerprint) that came but could not be kept
    bool mods_view = false;       // the list shows the battle mods
    bool local = false;           // Mode::Host without a network: the AI plays the other sides
    int mods_page = 0;
    MenuKeyLatch actions;
    bool dirty = true;
    // Over Epic Online Services (NetLink::eos): the host's room in the internet list; the join screen's look for the
    // rooms, every 5 s, and what it found.
    std::unique_ptr<NetLink::eos::Room> room;
    std::unique_ptr<NetLink::eos::RoomSearch> search;
    std::vector<NetLink::eos::RoomInfo> rooms;
    bool rooms_searched = false;
    std::chrono::steady_clock::time_point next_search{};
    // The lobby's password: the host's (password_file) or the one this person joins with (join_password_file).
    std::wstring password;
    bool asking_password = false;  // its screen is shown over the lobby or the join screen
    std::wstring password_typed;
    bool password_wrong = false;
    std::string settings_saved;  // the host's switches as last kept in their file (settings_text)
};

std::optional<State> state;
CMatrixHint *hint = nullptr;
bool battle_started = false;
bool name_saved = false;  // the person chose a name in this run: it wins over --lobby-name
std::map<std::wstring, int> sides_read;  // by map path, for the whole process (and kept in sides_file)
bool sides_loaded = false, sides_new = false;
int sides_files_read = 0;  // maps read since the lobby opened (not known from sides_file)
std::chrono::steady_clock::time_point maps_listed;
std::map<std::wstring, std::string> map_hashes;
std::map<std::wstring, std::vector<std::wstring>> map_lacks;  // MapPacks::missing, by map path, for the whole process
std::vector<std::unique_ptr<CBitmap>> lines;  // LobbyModel::separators(), made for lines_scale
double lines_scale = 0;
std::unique_ptr<CBitmap> toggles[2];  // a mod's switch, off and on, made for toggles_scale
std::unique_ptr<CBitmap> info_button;  // a mod's "i", made for toggles_scale
double toggles_scale = 0;

// A map file's size and time: one changed since is read again. "0 0": in the game's packages (they do not change; a
// loose map put in its place has a stamp of its own).
std::string file_stamp(const std::wstring &path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) return "0 0";
    const auto time = std::filesystem::last_write_time(path, error);
    return error ? "0 0" : std::format("{} {}", size, time.time_since_epoch().count());
}

// hashes_file: a line a map, "<hash>\t<path>" and "\t<file>\t<file_stamp>" for each file the hash covers (UTF-8). A
// fingerprint reads the map, a map mod's map its bundle (its cache.txt read, the files it takes found and read): up
// to tens of MB for each map chosen, again after every battle (the game starts again). Kept, it is read again only
// when one of its files changed.
struct KeptHash {
    std::string hash;
    std::vector<std::pair<std::wstring, std::string>> files;  // file, file_stamp
};
std::map<std::wstring, KeptHash> hashes_kept;
bool hashes_loaded = false;

void load_hashes() {
    if (std::exchange(hashes_loaded, true)) return;
    std::ifstream in{std::filesystem::path(hashes_file)};
    std::string line;
    while (std::getline(in, line)) {
        try {
            std::vector<std::string> parts;
            for (std::size_t at = 0;;) {
                const std::size_t tab = line.find('\t', at);
                parts.push_back(line.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
                if (tab == std::string::npos) break;
                at = tab + 1;
            }
            if (parts.size() < 4 || parts.size() % 2 != 0 || parts[0].empty()) continue;
            KeptHash kept{parts[0], {}};
            for (std::size_t i = 2; i + 1 < parts.size(); i += 2)
                kept.files.emplace_back(utils::to_wstring(parts[i]), parts[i + 1]);
            hashes_kept[utils::to_wstring(parts[1])] = std::move(kept);
        }
        catch (...) {
        }
    }
}

void save_hashes() {
    std::ofstream out{std::filesystem::path(hashes_file), std::ios::trunc};
    for (const auto &[path, kept] : hashes_kept) {
        out << kept.hash << '\t' << utils::from_wstring(path);
        for (const auto &[file, stamp] : kept.files) out << '\t' << utils::from_wstring(file) << '\t' << stamp;
        out << '\n';
    }
}

// sides_file: a line a map, "<size> <time> <sides> <path>" (UTF-8).
void load_sides() {
    if (std::exchange(sides_loaded, true)) return;
    std::ifstream in{std::filesystem::path(sides_file)};
    std::string size, time, line;
    int sides = 0;
    while (in >> size >> time >> sides && std::getline(in, line)) {
        if (line.size() < 2) continue;
        try {
            const std::wstring path = utils::to_wstring(std::string_view(line).substr(1));
            if (!sides_read.contains(path) && !MapShare::used(path) && file_stamp(path) == size + " " + time)
                sides_read[path] = sides;
        }
        catch (...) {
        }
    }
}

void save_sides() {
    if (!std::exchange(sides_new, false)) return;
    std::ofstream out{std::filesystem::path(sides_file), std::ios::trunc};
    // A map read from the host's copy (MapShare) is not kept: its own file, under the same name, may have other sides.
    for (const auto &[path, sides] : sides_read)
        if (sides > 0 && !MapShare::used(path))
            out << file_stamp(path) << ' ' << sides << ' ' << utils::from_wstring(path) << '\n';
}

LobbyModel::MapSort load_sort() {
    std::ifstream in{std::filesystem::path(sort_file)};
    std::string word;
    in >> word;
    return word == "people" ? LobbyModel::MapSort::People : LobbyModel::MapSort::Name;
}
LobbyModel::MapSort map_sort = LobbyModel::MapSort::Name;  // read when the lobby opens (the EXE's folder)

// The game's statistics line (stat_hl, or stat_vl standing) stretched to `length`: its visible part's
// ends kept (the picture has clear margins), its middle repeated.
std::unique_ptr<CBitmap> stretched_line(const CBitmap &line, int length, bool vertical) {
    const int along = vertical ? line.SizeY() : line.SizeX(), across = vertical ? line.SizeX() : line.SizeY();
    auto at = [&](int a) { return vertical ? CPoint(0, a) : CPoint(a, 0); };
    auto size = [&](int a) { return vertical ? CPoint(across, a) : CPoint(a, across); };
    auto seen = [&](int a) {
        for (int c = 0; c < across; ++c) {
            const int x = vertical ? c : a, y = vertical ? a : c;
            if (line.Data()[y * line.Pitch() + x * 4 + 3]) return true;
        }
        return false;
    };
    int first = 0, last = along - 1;
    while (first < last && !seen(first)) ++first;
    while (last > first && !seen(last)) --last;
    const int visible = last - first + 1, end = std::min(visible / 2, length / 2);
    auto out = std::make_unique<CBitmap>();
    if (vertical) out->CreateRGBA(across, length);
    else out->CreateRGBA(length, across);
    out->Copy(at(0), size(end), line, at(first));
    for (int a = end; a < length - end; ++a) out->Copy(at(a), size(1), line, at(first + visible / 2));
    out->Copy(at(length - end), size(end), line, at(last + 1 - end));
    return out;
}

// A hint bitmap of the game's: the battle preloads them, the lobby (before any battle) reads it itself.
const CBitmap *game_bitmap(const wchar_t *name, CBitmap &storage) {
    if (const CBitmap *loaded = CMatrixHint::FindBitmap(name)) return loaded;
    std::wstring path = g_MatrixData->BlockGet(PAR_SOURCE_HINTS)->BlockGet(PAR_SOURCE_HINTS_BITMAPS)->ParGetNE(name);
    std::wstring file;
    if (path.empty() || !CFile::FileExist(file, path.c_str(), L"png") || !storage.LoadFromPNG(file)) return nullptr;
    storage.SwapByte(CPoint(0, 0), storage.Size(), 0, 2);  // stored as [A]RGB, like the preloaded ones
    return &storage;
}

void prepare_lines(double scale) {
    if (!lines.empty() && lines_scale == scale) return;
    for (std::size_t i = 0; i < lines.size(); ++i) CMatrixHint::SetRuntimeBitmap(LobbyModel::line_bitmap(int(i)), nullptr);
    lines.clear();
    lines_scale = scale;
    CBitmap read_h, read_v;
    const CBitmap *horizontal = game_bitmap(L"stat_hl", read_h);
    const CBitmap *vertical = game_bitmap(L"stat_vl", read_v);
    if (!horizontal || !vertical) return;
    const auto &all = LobbyModel::separators();
    for (std::size_t i = 0; i < all.size(); ++i) {
        lines.push_back(stretched_line(all[i].vertical ? *vertical : *horizontal, LobbyModel::px(scale, all[i].length),
                                       all[i].vertical));
        CMatrixHint::SetRuntimeBitmap(LobbyModel::line_bitmap(int(i)), lines.back().get());
    }
}

void prepare_toggles(double scale) {
    if (toggles[0] && toggles_scale == scale) return;
    toggles_scale = scale;
    const int size = LobbyModel::px(scale, LobbyModel::Layout::toggle_size);
    for (int on = 0; on < 2; ++on) {
        toggles[on] = GameArt::bitmap(GameArt::picture(on ? GameArt::Button::SwitchOn : GameArt::Button::SwitchOff, size));
        CMatrixHint::SetRuntimeBitmap(LobbyModel::toggle_bitmap(on != 0), toggles[on].get());
    }
    info_button = GameArt::bitmap(GameArt::picture(GameArt::Button::Info, LobbyModel::px(scale, LobbyModel::Layout::info_size)));
    CMatrixHint::SetRuntimeBitmap(LobbyModel::info_bitmap(), info_button.get());
}

// The map file's fingerprint: the same name with other contents must not play together.
std::string map_hash(const std::wstring &path) {
    if (const auto known = map_hashes.find(path); known != map_hashes.end()) return known->second;
    // A copy the host gave; a map mod's map: its bundle (the map with the mod's files it takes).
    if (const auto kept = MapShare::used_hash(path)) return map_hashes[path] = *kept;
    load_hashes();
    if (const auto kept = hashes_kept.find(path);
        kept != hashes_kept.end() && std::all_of(kept->second.files.begin(), kept->second.files.end(),
                                                 [](const auto &f) { return file_stamp(f.first) == f.second; }))
        return map_hashes[path] = kept->second.hash;
    const auto began = std::chrono::steady_clock::now();
    KeptHash fresh;
    if (const MapPacks::Pack *pack = MapPacks::of_map(MapPacks::installed(), path)) {
        const auto files = MapPacks::bundle_files(path);
        for (const std::wstring &file : files) {
            const std::wstring full = (std::filesystem::path(pack->folder) / file).wstring();
            fresh.files.emplace_back(full, file_stamp(full));
        }
        const auto bundle = MapPacks::bundle(path, files);
        fresh.hash = bundle ? MapShare::fingerprint(*bundle) : std::string();
    }
    else {
        fresh.files.emplace_back(path, file_stamp(path));
        try {
            CBuf data;
            data.LoadFromFile(path);
            fresh.hash = GameData::hex(GameData::hash_bytes(data.Get(), std::size_t(data.Len())));
        }
        catch (...) {
        }
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count();
    if (ms >= 100) lgr.notice("Lobby: the fingerprint of {} took {} ms")(utils::from_wstring(path), ms);
    if (!fresh.hash.empty() && !fresh.files.empty()) {
        hashes_kept[path] = fresh;
        save_hashes();
    }
    return map_hashes[path] = fresh.hash;
}

Language language() {
    return GameLanguage::current();
}

void load_maps(State &s) {
    load_sides();
    sides_files_read = 0;
    maps_listed = std::chrono::steady_clock::now();
    s.maps = MapCatalog::scan();
    // The map mods' maps, named by their own Lang.dat.
    std::map<std::string, MapMetadata::Catalog> pack_names;
    for (const MapPacks::Pack &pack : MapPacks::installed()) {
        for (auto map : MapCatalog::listed(pack.path + L"\\" + MapPacks::maps_folder, MapPacks::map_files(pack))) {
            map.pack = pack.id;
            s.maps.push_back(std::move(map));
        }
        std::string error;
        if (const auto file = MapPacks::names_file(pack, s.language == Language::Russian); !file.empty())
            pack_names[pack.id] = MapMetadata::load(error, file);
        if (!error.empty()) lgr.warning("Map mod {}: names: {}")(pack.id, error);
    }
    std::string diagnostic;
    const auto metadata = MapMetadata::load(diagnostic, GameLanguage::names_file(L"", GameLanguage::current()));
    if (!diagnostic.empty()) lgr.warning("Map metadata: {}")(diagnostic);
    for (const auto &map : s.maps) {
        std::wstring key = map.name + L".cmap";
        for (auto &ch : key) if (ch >= L'A' && ch <= L'Z') ch += L'a' - L'A';
        const auto &names = map.pack.empty() ? metadata : pack_names[map.pack];
        const auto found = names.find(key);
        s.names.push_back(found == names.end() || found->second.name.empty() ? map.name
                                                                             : MapMetadata::plain_text(found->second.name));
        const auto known = sides_read.find(map.path);
        s.sides.push_back(known == sides_read.end() ? -1 : known->second);
    }
    // A map mod's map named as another map is told by its mod's name.
    std::vector<std::wstring> plain = s.names;
    for (auto &name : plain) for (auto &ch : name) ch = wchar_t(std::towlower(ch));
    for (std::size_t i = 0; i < s.maps.size(); ++i) {
        if (s.maps[i].pack.empty() || std::count(plain.begin(), plain.end(), plain[i]) < 2) continue;
        if (const MapPacks::Pack *pack = MapPacks::find(MapPacks::installed(), s.maps[i].pack))
            s.names[i] += L" (" + pack->name + L")";
    }
}

// "<id>:<fingerprint>" of the map mod this map comes from; empty: the game's map.
std::string pack_ref(const MapCatalog::Map &map) {
    const MapPacks::Pack *pack = map.pack.empty() ? nullptr : MapPacks::find(MapPacks::installed(), map.pack);
    return pack ? MapPacks::ref(*pack) : std::string();
}

// Why this PC cannot play the lobby's map: the host's map mod missing or different, files the map names lacking.
LobbyModel::MapProblem map_problem(const State &s, int selected, bool differs) {
    using Kind = LobbyModel::MapProblem::Kind;
    const auto &st = s.mode == Mode::Host ? s.host.state() : s.client.state();
    if (s.mode != Mode::Host && !st.map_pack.empty() && !MapShare::used(st.map)) {
        const std::string id = st.map_pack.substr(0, st.map_pack.find(':'));
        const MapPacks::Pack *pack = MapPacks::find(MapPacks::installed(), id);
        if (!pack) return {Kind::PackMissing, utils::to_wstring(id)};
        if (MapPacks::ref(*pack) != st.map_pack) return {Kind::PackDiffers, pack->name};
    }
    if (selected < 0 || differs) return {};
    const std::wstring &path = s.maps[std::size_t(selected)].path;
    auto known = map_lacks.find(path);
    if (known == map_lacks.end()) {
        known = map_lacks.emplace(path, MapPacks::missing(path)).first;
        if (!known->second.empty())
            lgr.warning("Lobby: the map {} lacks {}")(utils::from_wstring(path), utils::from_wstring(known->second[0]));
    }
    if (known->second.empty()) return {};
    return {Kind::FilesMissing, std::filesystem::path(known->second[0]).filename().wstring(), int(known->second.size())};
}

void read_sides(State &s, std::size_t index) {
    if (index >= s.sides.size() || s.sides[index] >= 0) return;
    s.sides[index] = int(MapSides::read(s.maps[index].path));
    sides_read[s.maps[index].path] = s.sides[index];
    sides_new = true;
    ++sides_files_read;
    s.dirty = true;
}

const Lobby::State &lobby(const State &s) { return s.mode == Mode::Host ? s.host.state() : s.client.state(); }

int selected(const State &s) { return lobby(s).map.empty() ? -1 : MapCatalog::find(s.maps, lobby(s).map); }

std::vector<int> chosen_sides(const Lobby::State &st) {
    std::vector<int> chosen;
    for (const Lobby::Person &p : st.people) chosen.push_back(p.side);
    return chosen;
}

unsigned map_sides(const State &s, int index) { return index >= 0 && s.sides[index] > 0 ? unsigned(s.sides[index]) : 0; }

// The battle mods: this PC's, each on when the lobby has it; a client's also lists the host's that it lacks.
std::vector<LobbyModel::ModRow> mod_rows(const State &s) {
    const auto &st = lobby(s);
    const bool ru = s.language == Language::Russian;
    std::vector<LobbyModel::ModRow> rows;
    for (const Mods::Mod &m : Mods::installed()) {
        if (m.kind != Mods::Kind::Battle) continue;
        const auto chosen = std::find_if(st.mods.begin(), st.mods.end(), [&](const Lobby::ModRef &r) { return r.id == m.id; });
        LobbyModel::ModRow row{ru ? m.name : m.name_en, ru ? m.description : m.description_en, chosen != st.mods.end()};
        if (row.on && s.mode != Mode::Host && chosen->hash != m.hash) row.problem = LobbyModel::ModRow::Problem::Differs;
        row.id = m.id;
        const auto window = ModInfo::of(m, Mods::installed(), s.language);
        row.author = window.author;
        row.depends = window.depends;
        row.conflicts = window.conflicts;
        rows.push_back(std::move(row));
    }
    if (s.mode != Mode::Host)
        for (const Lobby::ModRef &r : st.mods) {
            const Mods::Mod *m = Mods::find(Mods::installed(), r.id);
            if (!m || m->kind != Mods::Kind::Battle)
                rows.push_back({utils::to_wstring(r.id), {}, true, LobbyModel::ModRow::Problem::Missing});
        }
    if (s.mode == Mode::Host && s.local) return rows;  // nobody else to see
    // The host's switch, then one row of everyone's own mods: its "i" has a line a person who has some (names where
    // this PC has the mod; ones not built in marked).
    LobbyModel::ModRow verified{ru ? L"Только встроенные личные моды" : L"Only built-in personal mods",
                                ru ? L"В бою работают только те личные моды, что встроены в игру (в её папке mods, как "
                                     L"их собрала эта версия). Другие личные моды и изменённые встроенные у всех "
                                     L"игроков сами выключаются на этот бой; в настройках они остаются включёнными."
                                   : L"Only the personal mods built into the game (in its mods folder, as this version "
                                     L"built them) work in the battle. Everyone's other personal mods, and built-in "
                                     L"ones changed, are switched off for this battle by themselves; the settings keep "
                                     L"them on.",
                                st.verified_only};
    verified.kind = LobbyModel::ModRow::Kind::Verified;
    rows.push_back(std::move(verified));
    std::wstring people;
    for (std::size_t i = 0; i < st.people.size(); ++i) {
        const Lobby::Person &p = st.people[i];
        if (p.personal.empty()) continue;
        std::wstring list;
        const auto refused = Lobby::unverified(p);
        for (const Lobby::ModRef &r : p.personal) {
            const Mods::Mod *m = Mods::find(Mods::installed(), r.id);
            std::wstring name = m ? (ru ? m->name : m->name_en) : utils::to_wstring(r.id);
            if (std::find(refused.begin(), refused.end(), r) != refused.end())
                name += st.verified_only ? (ru ? L" (не встроенный, в бою выключится)" : L" (not built-in, off in the battle)")
                                         : (ru ? L" (не встроенный)" : L" (not built-in)");
            list += (list.empty() ? L"" : L", ") + name;
        }
        people += (people.empty() ? L"" : L"\n") +
                  (p.name.empty() ? (ru ? L"Игрок " : L"Person ") + std::to_wstring(i + 1) : p.name) + L": " + list;
    }
    LobbyModel::ModRow row{ru ? L"Личные моды игроков" : L"Players' personal mods",
                           people.empty() ? std::wstring(ru ? L"Ни у кого нет личных модов." : L"Nobody has personal mods.")
                                          : people};
    row.kind = LobbyModel::ModRow::Kind::People;
    rows.push_back(std::move(row));
    return rows;
}

// This person's own mods on, with this PC's fingerprints: told the lobby.
std::vector<Lobby::ModRef> own_personal(const StandaloneSettings::Values &values) {
    const auto ids = values.personal_mods ? *values.personal_mods : Mods::default_personal(Mods::installed());
    std::vector<Lobby::ModRef> refs;
    for (const Mods::Mod &m : Mods::installed())
        if (m.kind == Mods::Kind::Personal && m.problem == Mods::Problem::None &&
            std::find(ids.begin(), ids.end(), m.id) != ids.end())
            refs.push_back({m.id, m.hash});
    return refs;
}

// Installed battle mods with these ids, in the catalogue's order, with this PC's fingerprints.
std::vector<Lobby::ModRef> mod_refs(const std::vector<std::string> &ids) {
    std::vector<Lobby::ModRef> refs;
    for (const Mods::Mod &m : Mods::installed())
        if (m.kind == Mods::Kind::Battle && std::find(ids.begin(), ids.end(), m.id) != ids.end()) refs.push_back({m.id, m.hash});
    return refs;
}

LobbyModel::Screen screen(State &s) {
    LobbyModel::Screen view;
    view.language = s.language;
    view.host = s.mode == Mode::Host;
    view.local = s.local;
    for (std::size_t i = 0; i < s.maps.size(); ++i)
        view.maps.push_back({s.names[i], s.sides[i] < 0 ? -1 : MapSides::count(unsigned(s.sides[i]))});
    view.sort = map_sort;
    view.order = LobbyModel::map_order(view.maps, map_sort);
    const auto &st = lobby(s);
    view.people = st.people;
    view.you = view.host ? 0 : s.client.you();
    view.ai_resources = st.ai_resources;
    view.manual_control = st.manual_control;
    view.manual_bonus = st.manual_bonus;
    view.delay = st.delay;
    view.auto_delay = st.auto_delay;
    view.dropped_ai = st.dropped_ai;
    view.auto_resync = st.auto_resync;
    view.delay_follows = st.delay_follows;
    view.locked = s.mode == Mode::Host && s.host.locked();
    view.mods = mod_rows(s);
    // Mods on that cannot be on together, the battle's and this person's own: marked, and no start or ready.
    std::vector<std::string> on;
    for (const Lobby::ModRef &r : st.mods) on.push_back(r.id);
    for (const Lobby::ModRef &r : own_personal(s.values)) on.push_back(r.id);
    const auto name = [&](const std::string &id) {
        const Mods::Mod *m = Mods::find(Mods::installed(), id);
        return m ? (s.language == Language::Russian ? m->name : m->name_en) : utils::to_wstring(id);
    };
    for (const auto &[a, b] : Mods::conflicting(Mods::installed(), on)) {
        view.conflicts.push_back({name(a), name(b)});
        for (auto &row : view.mods)
            if (row.kind == LobbyModel::ModRow::Kind::Battle && row.on && (row.id == a || row.id == b) &&
                row.problem == LobbyModel::ModRow::Problem::None)
                row.problem = LobbyModel::ModRow::Problem::Conflict;
    }
    view.mods_view = s.mods_view;
    view.mods_page = std::clamp(s.mods_page, 0, LobbyModel::mods_page_count(view) - 1);
    view.scale = LobbyModel::scale_for(g_ScreenX, g_ScreenY);
    view.selected = selected(s);
    if (view.selected < 0 && !st.map.empty()) view.missing_map = std::filesystem::path(st.map).stem().wstring();
    if (!view.host && view.selected >= 0 && !st.map_hash.empty())
        view.map_differs = map_hash(s.maps[view.selected].path) != st.map_hash;
    view.map_problem = map_problem(s, view.selected, view.map_differs);
    if (!view.host) {
        view.map_progress = s.client.map_progress();
        view.map_failed = s.client.map_refused() || (!st.map_hash.empty() && s.map_failed == st.map_hash) ||
                          st.map_hash.empty() || !MapCatalog::valid_path(st.map);
    }
    view.page = view.host ? s.page : LobbyModel::page_of(LobbyModel::row_of(view, view.selected));
    // A room's host is reached through Epic: its eos:<id> means nothing to a person.
    view.address = !s.host_address.starts_with(L"eos:") ? s.host_address
                   : s.language == Language::Russian   ? L"через интернет (Epic)"
                                                       : L"over the internet (Epic)";
    view.loading = !view.host && s.client.phase() == NetLobby::Client::Phase::Loading;
    if (s.kick_asked >= int(st.people.size())) s.kick_asked = -1;  // (that person left meanwhile)
    view.kick_asked = view.host ? s.kick_asked : -1;
    MapPreview::show(view.selected >= 0 ? MapPreview::source(s.maps[view.selected]) : std::wstring{},
                     {LobbyModel::px(view.scale, LobbyModel::Layout::preview_width),
                      LobbyModel::px(view.scale, LobbyModel::Layout::preview_height)});
    view.preview = MapPreview::size();
    view.text_width = [](const std::wstring &text, const wchar_t *font) {
        return int(Text::GetFont(g_D3DD, font).CalcTextWidth(text));
    };
    return view;
}

void release_hint() {
    if (hint) hint->Release();
    hint = nullptr;
}

LobbyModel::JoinScreen join_view(const State &s) {
    LobbyModel::JoinScreen join;
    join.language = s.language;
    join.address = s.typed;
    join.connecting = s.client.phase() == NetLobby::Client::Phase::Connecting;
    join.error = s.error;
    join.scale = LobbyModel::scale_for(g_ScreenX, g_ScreenY);
    join.rooms_shown = NetLink::eos::available();
    join.searched = s.rooms_searched;
    for (const auto &r : s.rooms) {
        std::wstring created;
        if (r.created > 0) {
            const std::time_t t = std::time_t(r.created);
            std::tm local{};
            localtime_s(&local, &t);
            wchar_t text[16] = {};
            std::wcsftime(text, std::size(text), L"%H:%M", &local);
            created = text;
        }
        join.rooms.push_back({r.host, r.map, r.people, r.locked, created});
    }
    return join;
}

void rebuild() {
    State &s = *state;
    std::wstring markup;
    if (s.asking_password) {
        markup = LobbyModel::password_template({s.language, s.password_typed, s.mode == Mode::Host, s.password_wrong,
                                                LobbyModel::scale_for(g_ScreenX, g_ScreenY)});
    }
    else if (s.mode == Mode::Name) {
        markup = LobbyModel::name_template({s.language, s.name, s.name_forced, LobbyModel::scale_for(g_ScreenX, g_ScreenY)});
    }
    else if (s.mode == Mode::Join) {
        MapPreview::release();
        markup = LobbyModel::join_template(join_view(s));
    }
    else {
        const LobbyModel::Screen view = screen(s);
        prepare_lines(view.scale);
        prepare_toggles(view.scale);
        markup = LobbyModel::hint_template(view);
    }
    CMatrixHint *built = CMatrixHint::Build(markup, static_cast<CBlockPar *>(nullptr));
    built->Show((g_ScreenX - built->m_Width) / 2, (g_ScreenY - built->m_Height) / 2);
    release_hint();
    hint = built;
    ModInfoWindow::raise();
    s.dirty = false;
}

constexpr const wchar_t *password_file = L"CFG\\lobby-password.txt";      // the host's
constexpr const wchar_t *join_password_file = L"CFG\\join-password.txt";  // the last one joined with

std::wstring load_text(const wchar_t *file) {
    std::ifstream in{std::filesystem::path(file)};
    std::string line;
    std::getline(in, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    try {
        return utils::to_wstring(line);
    }
    catch (...) {
        return {};  // not UTF-8 (the file was edited by hand): as no text
    }
}

void save_text(const wchar_t *file, const std::wstring &text) {
    std::ofstream out{std::filesystem::path(file), std::ios::trunc};
    out << utils::from_wstring(text) << '\n';
}

// The host's last switches (AI resources, robot control and its bonus, delay, a dropped person's side, desync,
// the delay in the battle): the network lobby's and the single player's each in their file, read when it opens.
const wchar_t *settings_file(const State &s) { return s.local ? L"CFG\\local-settings.txt" : L"CFG\\lobby-settings.txt"; }

std::string settings_text(const Lobby::State &st) {
    return std::format("ai={} manual={} manual_bonus={} delay={} dropped={} resync={} follow={}", st.ai_resources,
                       st.manual_control ? 1 : 0, st.manual_bonus ? 1 : 0, st.delay, st.dropped_ai ? 1 : 0,
                       st.auto_resync ? 1 : 0, st.delay_follows ? 1 : 0);
}

void load_settings(State &s) {
    if (s.options.lobby_start > 0) return;  // a test's lobby (--lobby-start): its command line only
    std::ifstream in{std::filesystem::path(settings_file(s))};
    std::string line;
    if (!std::getline(in, line)) return;
    std::map<std::string, int> v;
    std::istringstream words{line};
    for (std::string word; words >> word;)
        if (const auto eq = word.find('='); eq != std::string::npos) v[word.substr(0, eq)] = std::atoi(word.c_str() + eq + 1);
    const auto st = s.host.state();
    const auto get = [&](const char *key, int fallback) { return v.contains(key) ? v[key] : fallback; };
    s.host.set_ai_resources(std::clamp(get("ai", st.ai_resources), 0, BattleRules::max_ai_resources));
    s.host.set_manual(get("manual", st.manual_control) != 0, get("manual_bonus", st.manual_bonus) != 0);
    if (!s.local) {
        s.host.set_delay(std::max(0, get("delay", st.delay)));
        s.host.set_dropped_ai(get("dropped", st.dropped_ai) != 0);
        s.host.set_auto_resync(get("resync", st.auto_resync) != 0);
        s.host.set_delay_follows(get("follow", st.delay_follows) != 0);
    }
    s.settings_saved = settings_text(s.host.state());
}

// Every frame of a host's lobby: its switches into their file when one changed.
void keep_settings(State &s) {
    if (s.options.lobby_start > 0) return;
    const std::string now = settings_text(s.host.state());
    if (now == s.settings_saved) return;
    std::ofstream out{std::filesystem::path(settings_file(s)), std::ios::trunc};
    out << now << '\n';
    s.settings_saved = now;
}

std::wstring saved_address() {
    std::ifstream in{std::filesystem::path(address_file)};
    std::string line;
    std::getline(in, line);
    std::wstring text;
    for (char ch : line)
        if (LobbyModel::address_char(wchar_t(ch)) && text.size() < LobbyModel::address_length) text += wchar_t(ch);
    return text;
}

void save_address(const std::wstring &text) {
    if (text.starts_with(L"eos:")) return;  // a room's host, chosen in the list: not typed
    std::ofstream out{std::filesystem::path(address_file), std::ios::trunc};
    out << utils::from_wstring(text) << '\n';
}

// Ends the startup menu; the game loads the prepared battle.
void finish() {
    battle_started = true;
    if (!BattleLoading::show(g_Wnd, state->language == Language::Russian))
        lgr.warning("Cannot create the loading screen; continuing battle initialization");
    ModInfoWindow::close();
    release_hint();
    MapPreview::release();
    state.reset();
    SETFLAG(g_Flags, GFLAG_EXITLOOP);
}

std::wstring own_name(const State &s) { return player_name(s.options); }

void start_host() {
    State &s = *state;
    LobbyModel::Screen view = screen(s);
    if (!LobbyModel::can_start(view)) return;
    const auto &st = s.host.state();
    const auto seed = s.options.seed.value_or(static_cast<std::uint32_t>(std::time(nullptr)));
    // Coinciding choices are drawn now, with the battle's seed.
    const std::vector<int> sides = MapSides::resolve(map_sides(s, view.selected), chosen_sides(st), seed);
    if (sides.size() != st.people.size()) return;
    if (!MapCatalog::readable(st.map)) {
        lgr.error("Lobby: the map {} cannot be loaded")(utils::from_wstring(st.map));
        return;
    }
    NetBattle::Start start = NetBattle::lobby_start(st, sides, s.values, seed,
                                                     Lobby::delay_turns(st, Lockstep::Config{}.turn_ms));
    if (s.local) {
        // Alone against the AI: the game's own loop runs the battle (NetBattle::local_battle). The next lobby opens
        // on this map with this bonus.
        lgr.notice("Lobby: starting {} against the AI")(utils::from_wstring(st.map));
        auto values = s.values;
        values.map = st.map;
        values.battle_bonus = s.bonus;
        std::string error;
        if (!StandaloneSettings::save(values, error)) lgr.warning("Lobby: cannot keep the map chosen: {}")(error);
        NetBattle::Options options = s.options;
        options.role = NetBattle::Role::None;
        options.lobby_local = true;
        NetBattle::prepare(std::move(options), std::move(start), {});
        finish();
        return;
    }
    lgr.notice("Lobby: starting {} for {} people, delay {} turns")(utils::from_wstring(st.map), st.people.size(),
                                                                  start.config.delay);
    // NetBattle::run gives the clients' links the human sides in increasing order: the drawn sides
    // need not follow the joining order.
    auto joined = s.host.start();
    std::vector<std::size_t> order(joined.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return sides[a + 1] < sides[b + 1]; });
    std::vector<std::unique_ptr<NetLink::Connection>> links;
    for (std::size_t i : order) links.push_back(std::move(joined[i]));
    NetBattle::Options options = s.options;
    options.role = NetBattle::Role::Host;
    options.address = s.listen_address;  // where the lobby opens again after the battle
    options.port = s.listen_port;
    NetBattle::prepare(std::move(options), std::move(start), std::move(links));
    finish();
}

void start_client() {
    State &s = *state;
    lgr.notice("Lobby: the host started {}")(utils::from_wstring(s.client.start()->map));
    std::vector<std::unique_ptr<NetLink::Connection>> links;
    links.push_back(s.client.take_link());
    NetBattle::Options options = s.options;
    options.role = NetBattle::Role::Client;
    if (const auto endpoint = LobbyModel::parse_endpoint(s.host_address)) {  // the lobby to rejoin after the battle
        options.address = endpoint->address;
        options.port = endpoint->port;
    }
    NetBattle::prepare(std::move(options), *s.client.start(), std::move(links));
    finish();
}

JoinError join_error(NetLobby::Client::Failure failure) {
    using F = NetLobby::Client::Failure;
    switch (failure) {
        case F::Unreachable: return JoinError::Unreachable;
        case F::Full: return JoinError::Full;
        case F::Version: return JoinError::Version;
        case F::Build: return JoinError::Build;
        case F::Data: return JoinError::Data;
        case F::Name: return JoinError::Name;
        case F::Started:
        case F::ComeBack: return JoinError::Started;
        case F::HostLeft: return JoinError::HostLeft;
        case F::Lost: return JoinError::Lost;
        case F::Kicked: return JoinError::Kicked;
        case F::Password: return JoinError::Password;
        case F::None: break;
    }
    return JoinError::None;
}

void connect() {
    State &s = *state;
    if (s.client.phase() == NetLobby::Client::Phase::Connecting) return;
    const auto endpoint = LobbyModel::parse_endpoint(s.typed);
    if (!endpoint) {
        s.error = JoinError::BadAddress;
        s.dirty = true;
        return;
    }
    lgr.notice("Lobby: joining {}:{}")(endpoint->address, endpoint->port);
    s.client.set_password(Lobby::password_hash(s.password));
    // Back from a battle the host may still be reading the statistics: wait for its lobby up to ten
    // minutes (Back stops waiting).
    // Over EOS the first connection to a person (NAT punch-through, else a relay) may take 10-20 s.
    const int timeout_ms = NetLink::eos::is(endpoint->address) ? 30000 : connect_timeout_ms;
    s.client.connect(endpoint->address, endpoint->port, s.bonus, s.options.lobby_return ? 600000 : timeout_ms,
                     GameData::data_fingerprint(),
                     GameData::build_fingerprint(), own_name(s), own_personal(s.values));
    s.error = JoinError::None;
    s.dirty = true;
}

void select_map(State &s, int index) {
    if (!LobbyModel::offered(screen(s), index)) return;
    s.host.set_map(s.maps[index].path, map_sides(s, index), map_hash(s.maps[index].path), pack_ref(s.maps[index]));
    s.dirty = true;
}

// This person's next choice in that direction on the lobby's map: any (0), then its playable sides.
// Others may have chosen the same side: the start draws among them.
void cycle_side(State &s, int delta) {
    const auto view = screen(s);
    const unsigned playable = map_sides(s, view.selected);
    const auto &people = lobby(s).people;
    if (!playable || view.you < 0 || view.you >= int(people.size())) return;
    const int current = people[view.you].side;
    for (int step = 1; step <= 4; ++step) {
        const int side = ((current + delta * step) % 5 + 5) % 5;
        if (side && !(playable & (1u << side))) continue;
        if (s.mode == Mode::Host) s.host.choose_side(0, side);
        else s.client.set_side(side);
        s.dirty = true;
        return;
    }
}

// The next offered map from the lobby's one in this direction down the list.
void move_map(State &s, int delta) {
    const auto view = screen(s);
    for (int row = LobbyModel::row_of(view, view.selected) + delta; row >= 0 && row < int(s.maps.size()); row += delta) {
        if (!LobbyModel::offered(view, view.order[row])) continue;
        select_map(s, view.order[row]);
        s.page = LobbyModel::page_of(row);
        return;
    }
}

// Another page of the mods' list (whoever looks at it: a client's too).
void page_mods(State &s, int delta) {
    s.mods_page = LobbyModel::mods_page_moved(screen(s), delta);
    s.dirty = true;
}

// The host's list shows the page with the lobby's map.
void page_to_selected(State &s) {
    const auto view = screen(s);
    if (view.selected >= 0) s.page = LobbyModel::page_of(LobbyModel::row_of(view, view.selected));
}

void set_sort(State &s, LobbyModel::MapSort sort) {
    if (sort == map_sort) return;
    map_sort = sort;
    std::ofstream out{std::filesystem::path(sort_file), std::ios::trunc};
    out << (sort == LobbyModel::MapSort::People ? "people" : "name") << '\n';
    page_to_selected(s);
    s.dirty = true;
}

void set_bonus(State &s, int index) {
    if (index < 0 || index > 2) return;
    s.bonus = static_cast<BattleChoice::Bonus>(index);
    if (s.mode == Mode::Host) s.host.set_bonus(s.bonus);
    else s.client.set_bonus(s.bonus);
    s.dirty = true;
}

// The host's battle mods now these, kept for its next lobbies and own battles.
void set_mods(State &s, const std::vector<std::string> &ids) {
    s.host.set_mods(mod_refs(ids));
    s.values.mods = ids;
    std::string error;
    if (!StandaloneSettings::save(s.values, error)) lgr.warning("Lobby: cannot keep the mods chosen: {}")(error);
    lgr.notice("Lobby: battle mods {}")(Mods::join(ids));
    s.dirty = true;
}

// The host switches a battle mod (the row's place among the installed battle mods). Off, what needs it goes too;
// on, when it needs mods that are off, the host is asked to switch them on with it (no: it stays off). Mods that
// conflict may be on together: the lobby then names them and does not start (Screen::conflicts).
void toggle_mod(State &s, int row) {
    std::vector<std::string> ids;
    for (const auto &r : s.host.state().mods) ids.push_back(r.id);
    const Mods::Mod *mod = nullptr;
    int index = 0;
    for (const Mods::Mod &m : Mods::installed())
        if (m.kind == Mods::Kind::Battle && index++ == row) mod = &m;
    if (!mod) return;
    const auto needed = Mods::needs(Mods::installed(), ids, mod->id);
    if (std::find(ids.begin(), ids.end(), mod->id) != ids.end() || needed.empty()) {
        set_mods(s, Mods::toggled(Mods::installed(), ids, mod->id));
        return;
    }
    const bool ru = s.language == Language::Russian;
    ModInfo::Question question{ru ? mod->name : mod->name_en};
    for (const auto &id : needed) {
        const Mods::Mod *need = Mods::find(Mods::installed(), id);
        question.needs.push_back(ru ? need->name : need->name_en);
    }
    ModInfoWindow::ask(question, s.language, LobbyModel::scale_for(g_ScreenX, g_ScreenY), [id = mod->id](bool yes) {
        if (!yes || !state || state->mode != Mode::Host) return;
        std::vector<std::string> now;
        for (const auto &r : state->host.state().mods) now.push_back(r.id);
        if (std::find(now.begin(), now.end(), id) == now.end()) set_mods(*state, Mods::toggled(Mods::installed(), now, id));
    });
}

void primary(State &s) {
    if (s.mode == Mode::Host) {
        start_host();
        return;
    }
    const auto view = screen(s);
    if (view.loading || !view.missing_map.empty() ||
        (!s.client.ready() && (view.map_differs || !LobbyModel::offered(view, view.selected) || !LobbyModel::mods_fine(view) ||
                               !LobbyModel::map_fine(view))))
        return;
    s.client.set_ready(!s.client.ready());
}

// The host's map when this PC lacks its file or has another: a copy kept from before (a battle of this lobby),
// else the host sends it. The copy is read under the host's name until the lobby or battle is over.
void share_map(State &s) {
    const auto &st = s.client.state();
    if (st.map.empty() || st.map_hash.empty() || !MapCatalog::valid_path(st.map) || s.map_failed == st.map_hash) return;
    const int index = selected(s);
    if (index >= 0 && map_hash(s.maps[index].path) == st.map_hash) return;
    if (!MapShare::use(st.map, st.map_hash)) {
        auto bytes = s.client.take_map();
        if (!bytes) {
            s.client.request_map(st.map_hash);
            return;
        }
        if (!MapShare::store(st.map_hash, *bytes) || !MapShare::use(st.map, st.map_hash)) {
            lgr.warning("Lobby: cannot keep the host's map {} in {}")(utils::from_wstring(st.map),
                                                                     utils::from_wstring(MapShare::folder));
            s.map_failed = st.map_hash;
            s.dirty = true;
            return;
        }
    }
    lgr.notice("Lobby: the host's map {} is read from its copy {}")(utils::from_wstring(st.map), st.map_hash);
    map_hashes.erase(st.map);
    sides_read.erase(st.map);
    if (index >= 0) {
        s.sides[std::size_t(index)] = -1;
    }
    else {
        // A map mod's map the host gave is named by the mod's Lang.dat that came with it.
        std::wstring name = std::filesystem::path(st.map).stem().wstring();
        if (const MapPacks::Pack *pack = MapPacks::for_map(st.map)) {
            std::string error;
            const auto names = MapMetadata::load(error, MapPacks::names_file(*pack, s.language == Language::Russian));
            std::wstring key = std::filesystem::path(st.map).filename().wstring();
            for (auto &ch : key) if (ch >= L'A' && ch <= L'Z') ch += L'a' - L'A';
            if (const auto found = names.find(key); found != names.end() && !found->second.name.empty())
                name = MapMetadata::plain_text(found->second.name);
        }
        s.maps.push_back({name, st.map, {}, true, {}});
        s.names.push_back(name);
        s.sides.push_back(-1);
    }
    s.dirty = true;
}

void track_client(State &s) {
    using Phase = NetLobby::Client::Phase;
    const Phase phase = s.client.phase();
    if (phase == Phase::Started) {
        start_client();
        return;
    }
    if (phase == Phase::Failed && (s.mode == Mode::Client || s.error == JoinError::None)) {
        // Back to the address screen with the reason (an empty one: closed here).
        // The battle has started: one this person dropped out of is come back to (the game starts again).
        const auto ticket = NetBattle::rejoin_ticket();
        const auto endpoint = LobbyModel::parse_endpoint(s.typed);
        const bool own_ticket = ticket && endpoint && ticket->address == endpoint->address && ticket->port == endpoint->port;
        if (endpoint && (s.client.failure() == NetLobby::Client::Failure::ComeBack ||
                         (s.client.failure() == NetLobby::Client::Failure::Started && own_ticket))) {
            lgr.notice("Lobby: the battle has started; coming back to it");
            NetBattle::request_rejoin_at(endpoint->address, endpoint->port, player_name(s.options));
            // (Like the menu's Back to the battle: the game ends here and starts again with --net-rejoin.)
            SETFLAG(g_Flags, GFLAG_EXITLOOP);
            return;
        }
        if (s.client.failure() != NetLobby::Client::Failure::None) {
            MapShare::forget();
            s.error = join_error(s.client.failure());
            if (s.error == JoinError::Password) {  // asked for it (again)
                s.password_wrong = !s.password.empty();
                s.password_typed = s.password;
                s.asking_password = true;
            }
            lgr.warning("Lobby: left the lobby ({})")(int(s.client.failure()));
            s.mode = Mode::Join;
            s.dirty = true;
        }
        return;
    }
    if (phase == Phase::InLobby && s.mode == Mode::Join) {
        s.mode = Mode::Client;
        s.host_address = s.typed;
        save_address(s.typed);
        lgr.notice("Lobby: joined as person {}")(s.client.you() + 1);
        s.dirty = true;
    }
    if (s.mode != Mode::Client || phase != Phase::InLobby) return;
    share_map(s);
    const auto view = screen(s);
    const bool can_ready = view.missing_map.empty() && !view.map_differs && LobbyModel::offered(view, view.selected) &&
                           LobbyModel::mods_fine(view) && LobbyModel::map_fine(view);
    if (!can_ready) s.auto_ready_map.clear();
    if (s.client.ready() && !can_ready) s.client.set_ready(false);
    // (After --lobby-side's request: the host reads them in order.)
    if (s.options.lobby_ready && !s.client.ready() && can_ready && s.auto_ready_map != s.client.state().map &&
        (!s.options.lobby_side || s.side_asked)) {
        s.client.set_ready(true);
        s.auto_ready_map = s.client.state().map;
    }
}

void poll_network(State &s) {
    if (s.mode == Mode::Host) {
        if (s.host.poll()) s.dirty = true;
        for (const auto &event : s.host.take_events()) lgr.notice("Lobby: {}")(event);
        const int map = selected(s);
        if (s.options.lobby_start > 0 && s.host.people() >= s.options.lobby_start && map >= 0 && s.sides[map] >= 0 &&
            (!s.options.lobby_side || s.side_asked) &&
            LobbyModel::can_start(screen(s)))
            start_host();
        if (state) keep_settings(s);
        if (s.room && state) {
            const int most = map >= 0 && s.sides[map] > 0 ? MapSides::count(unsigned(s.sides[map])) : 4;
            s.room->set(own_name(s), map >= 0 ? s.names[map] : L"-",
                        std::to_wstring(s.host.people()) + L"/" + std::to_wstring(most), s.host.locked());
            s.room->pump();
        }
        return;
    }
    if (s.mode == Mode::Join && NetLink::eos::available()) {
        const auto now = std::chrono::steady_clock::now();
        if (!s.search && now >= s.next_search)
            s.search = std::make_unique<NetLink::eos::RoomSearch>(GameData::build_fingerprint());
        if (s.search && s.search->poll()) {
            const auto &found = s.search->rooms();
            const auto same = [](const NetLink::eos::RoomInfo &a, const NetLink::eos::RoomInfo &b) {
                return a.address == b.address && a.port == b.port && a.host == b.host && a.map == b.map &&
                       a.people == b.people && a.locked == b.locked && a.created == b.created;
            };
            if (!s.rooms_searched || !std::equal(found.begin(), found.end(), s.rooms.begin(), s.rooms.end(), same)) {
                s.rooms = found;
                s.dirty = true;
            }
            s.rooms_searched = true;
            s.search.reset();
            s.next_search = now + std::chrono::seconds(5);
        }
    }
    if (s.client.phase() != NetLobby::Client::Phase::Failed && s.client.poll()) s.dirty = true;
    track_client(s);
}

void open(Mode mode, const StandaloneSettings::Values &values, const NetBattle::Options &options, bool local = false) {
    shutdown();
    battle_started = false;
    state.emplace();
    State &s = *state;
    s.mode = mode;
    s.values = values;
    s.options = options;
    s.local = local;
    s.language = language();
    s.bonus = options.lobby_bonus.value_or(values.battle_bonus);
    s.actions = {Input::isVKeyPressed(VK_RETURN) || (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0,
                 Input::isVKeyPressed(VK_ESCAPE) || (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0};
    if (mode == Mode::Name) return;
    if (!local) {  // a network's lobby goes on in the background: no exclusive mode while it is open
        s.exclusive_was = Display::exclusive_allowed();
        s.display_changed = true;
        Display::set_exclusive_allowed(false);
    }
    load_maps(s);
    map_sort = load_sort();
}

std::wstring lobby_map(const NetBattle::Options &options, const StandaloneSettings::Values &values) {
    std::wstring map = !options.lobby_map.empty() ? options.lobby_map : values.map;
    if (map.empty()) return map;
    if (map.find(L'\\') == std::wstring::npos) map = std::wstring(MapCatalog::folder) + L"\\" + map;
    if (!std::filesystem::path(map).has_extension()) map += L".cmap";
    return map;
}

// The character a key types in the current keyboard layout, or 0.
wchar_t typed_char(uint8_t vk) {
    BYTE keys[256] = {};
    if (!GetKeyboardState(keys)) return 0;
    wchar_t buffer[4] = {};
    const int n = ToUnicode(vk, MapVirtualKeyW(vk, MAPVK_VK_TO_VSC), keys, buffer, 4, 0);
    if (n == 1) return buffer[0];
    if (vk >= '0' && vk <= '9') return wchar_t(vk);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return wchar_t(L'0' + vk - VK_NUMPAD0);
    if (vk == VK_DECIMAL) return L'.';
    return 0;
}

void ask_host_password(State &s) {
    s.asking_password = true;
    s.password_wrong = false;
    s.password_typed = s.password;
    s.dirty = true;
}

void close_password(State &s) {
    s.asking_password = false;
    s.password_wrong = false;
    s.dirty = true;
}

// The host's: the lobby's password from now on (kept for its next lobbies); the joining person's: connects with it.
void save_password(State &s) {
    s.password = s.password_typed;
    close_password(s);
    if (s.mode == Mode::Host) {
        s.host.set_password(Lobby::password_hash(s.password));
        save_text(password_file, s.password);
        lgr.notice(s.password.empty() ? "Lobby: no password" : "Lobby: a password set");
        return;
    }
    save_text(join_password_file, s.password);
    if (s.mode == Mode::Join) connect();
}

void type(State &s, const std::wstring &text) {
    if (s.asking_password) {
        for (wchar_t ch : text)
            if (LobbyModel::password_char(ch) && s.password_typed.size() < LobbyModel::password_length) s.password_typed += ch;
        s.dirty = true;
        return;
    }
    if (s.mode == Mode::Name) {
        for (wchar_t ch : text)
            if (PlayerName::valid_char(ch) && s.name.size() < PlayerName::max_length) s.name += ch;
        s.dirty = true;
        return;
    }
    if (s.typed.starts_with(L"eos:")) s.typed.clear();  // a room was chosen: typing starts an address
    for (wchar_t ch : text) {
        if (ch == L',') ch = L'.';  // the decimal key of some layouts
        if (LobbyModel::address_char(ch) && s.typed.size() < LobbyModel::address_length) s.typed += ch;
    }
    s.error = JoinError::None;
    s.dirty = true;
}

void paste(State &s) {
    if (!OpenClipboard(g_Wnd)) return;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto *text = static_cast<const wchar_t *>(GlobalLock(data))) {
            std::wstring clean;
            for (const wchar_t *p = text; *p && clean.size() < 64; ++p)
                if (*p != L' ' && *p != L'\t' && *p != L'\r' && *p != L'\n') clean += *p;
            GlobalUnlock(data);
            if (s.mode == Mode::Name) s.name.clear();
            else s.typed.clear();
            type(s, clean);
        }
    }
    CloseClipboard();
}

// The host's choices as its lobby opens: the last map, direct control, the battle mods.
void first_choices(State &s, const StandaloneSettings::Values &values, const NetBattle::Options &options) {
    const std::wstring map = lobby_map(options, values);
    if (const int index = MapCatalog::find(s.maps, map); index >= 0) {
        s.host.set_map(s.maps[index].path, map_sides(s, index), map_hash(s.maps[index].path), pack_ref(s.maps[index]));
        page_to_selected(s);
    }
    s.host.set_manual(options.manual_control != 0, options.manual_bonus != 0);  // --net-manual (tests)
    // The battle mods: --net-mods (tests), else the ones last chosen here (or the mods' defaults).
    const auto mods = options.mods ? *options.mods : values.mods ? *values.mods : Mods::default_battle(Mods::installed());
    s.host.set_mods(mod_refs(mods));
}

void save_name(State &s) {
    if (!PlayerName::valid(s.name)) return;
    if (!PlayerName::save(s.name)) {
        lgr.error("Cannot save the player name");
        return;
    }
    lgr.notice("Player name saved");
    name_saved = true;
    shutdown();
}

}  // namespace

bool active() { return state.has_value() && hint != nullptr; }

bool open_host(const StandaloneSettings::Values &values, const NetBattle::Options &options) {
    open(Mode::Host, values, options);
    State &s = *state;
    const std::string address = options.lobby ? options.address : "0.0.0.0";
    const int port = options.lobby ? options.port : Lobby::default_port;
    if (!s.host.open(address, port, s.bonus, GameData::data_fingerprint(), GameData::build_fingerprint(), own_name(s),
                     own_personal(values))) {
        lgr.error("Lobby: cannot listen on {}:{}")(address, port);
        state.reset();
        return false;
    }
    lgr.notice("Lobby: hosting on {}:{}")(address, port);
    s.password = options.lobby_password_given ? options.lobby_password : load_text(password_file);  // --lobby-password
    s.host.set_password(Lobby::password_hash(s.password));
    if (address == "0.0.0.0" && NetLink::eos::available())  // the room in the internet list
        s.room = std::make_unique<NetLink::eos::Room>(port, GameData::build_fingerprint());
    s.listen_address = address;
    s.listen_port = port;
    if (address == "0.0.0.0") {
        // Every address of this PC (a LAN's, a VPN's or a virtual LAN's), a line each in the lobby.
        for (const auto &a : NetLink::local_addresses()) {
            if (std::count(s.host_address.begin(), s.host_address.end(), L',') >= 2) break;  // three: the switches above need the room
            s.host_address += (s.host_address.empty() ? L"" : L", ") + utils::to_wstring(a) + L":" + std::to_wstring(port);
        }
        if (s.host_address.empty()) s.host_address = L"127.0.0.1:" + std::to_wstring(port);
    }
    else {
        s.host_address = utils::to_wstring(address) + L":" + std::to_wstring(port);
    }
    first_choices(s, values, options);
    if (options.delay > 0) s.host.set_delay(options.delay * Lockstep::Config{}.turn_ms);  // --net-delay
    s.host.set_dropped_ai(options.dropped_ai != 0);                                       // --net-dropped
    s.host.set_delay_follows(options.delay_follows != 0);  // --net-delay-follows
    s.host.set_auto_resync(options.auto_resync != 0);                                     // --net-resync
    load_settings(s);
    rebuild();
    return true;
}

void open_local(const StandaloneSettings::Values &values, const NetBattle::Options &options) {
    open(Mode::Host, values, options, true);
    State &s = *state;
    s.host.open_local(s.bonus, own_name(s), own_personal(values));
    lgr.notice("Lobby: a battle against the AI");
    first_choices(s, values, options);
    load_settings(s);
    rebuild();
}

void open_join(const StandaloneSettings::Values &values, const NetBattle::Options &options) {
    open(Mode::Join, values, options);
    State &s = *state;
    s.typed = options.lobby ? utils::to_wstring(options.address) + L":" + std::to_wstring(options.port) : saved_address();
    s.password = options.lobby_password_given ? options.lobby_password : load_text(join_password_file);  // --lobby-password
    if (options.lobby) connect();
    rebuild();
}

void open_name(bool forced) {
    open(Mode::Name, {}, {});
    State &s = *state;
    s.name = PlayerName::load();
    s.name_forced = forced;
    rebuild();
}

void poll() {
    if (state && state->mode != Mode::Name) poll_network(*state);
}

void takt() {
    if (!state) return;
    State &s = *state;
    {
        // For lag reports: a frame of the lobby that took long (its window answers nothing meanwhile).
        const auto now = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - s.last_takt).count();
        if (s.last_takt != std::chrono::steady_clock::time_point{} && ms >= 300) lgr.notice("Lobby: a slow frame, {} ms")(ms);
        s.last_takt = now;
    }
    if (s.mode == Mode::Name) {
        if (s.dirty) rebuild();
        return;
    }
    poll_network(s);
    if (!state) return;  // a battle started
    // The lobby's map first, then the rest a little every frame.
    if (const int index = selected(s); index >= 0) {
        read_sides(s, std::size_t(index));
        if (s.mode == Mode::Host) s.host.set_map_sides(map_sides(s, index));  // side choices need them
        if (s.options.lobby_side && !s.side_asked && map_sides(s, index) && s.mode != Mode::Join) {
            s.side_asked = true;
            if (s.mode == Mode::Host) s.host.choose_side(0, s.options.lobby_side);
            else s.client.set_side(s.options.lobby_side);
            s.dirty = true;
        }
    }
    const auto until = std::chrono::steady_clock::now() + map_read_budget;
    const bool reading = s.next_read < s.maps.size();
    while (s.next_read < s.maps.size() && std::chrono::steady_clock::now() < until) read_sides(s, s.next_read++);
    if (reading && s.next_read >= s.maps.size()) {
        lgr.notice("Lobby: the sides of {} maps known, {} of them read now, in {} ms")(
            s.maps.size(), sides_files_read,
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - maps_listed).count());
        save_sides();  // all read: the next lobby (and run) need not
    }
    // By people, rows move while the maps are read: the host's page follows the lobby's map.
    if (reading && map_sort == LobbyModel::MapSort::People && s.mode == Mode::Host) page_to_selected(s);
    if (s.dirty) rebuild();
}

bool keyboard(uint8_t vk, bool down) {
    if (!active()) return false;
    State &s = *state;
    if (!s.actions.accept(vk, down)) return true;
    if (ModInfoWindow::open()) {  // a mod's window or its question: nothing else happens meanwhile
        ModInfoWindow::key(vk);
        return true;
    }
    if (s.asking_password) {
        if (vk == VK_ESCAPE) close_password(s);
        else if (vk == VK_RETURN) save_password(s);
        else if (vk == VK_BACK) {
            if (!s.password_typed.empty()) s.password_typed.pop_back();
            s.dirty = true;
        }
        else if (vk == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) paste(s);
        else if (const wchar_t ch = typed_char(vk)) type(s, std::wstring(1, ch));
        return true;
    }
    if (s.mode == Mode::Host && !s.local && vk == 'P') {  // the lobby's password
        ask_host_password(s);
        return true;
    }
    if (s.mode == Mode::Name) {
        if (vk == VK_ESCAPE) {
            if (!s.name_forced) shutdown();
        }
        else if (vk == VK_RETURN) save_name(s);
        else if (vk == VK_BACK) {
            if (!s.name.empty()) s.name.pop_back();
            s.dirty = true;
        }
        else if (vk == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) paste(s);
        else if (const wchar_t ch = typed_char(vk)) type(s, std::wstring(1, ch));
        return true;
    }
    if (s.mode == Mode::Join) {
        const bool connecting = s.client.phase() == NetLobby::Client::Phase::Connecting;
        if (vk == VK_ESCAPE) shutdown();
        else if (vk == VK_RETURN) connect();
        else if (connecting) return true;
        else if (vk == VK_BACK) {
            if (!s.typed.empty()) s.typed.pop_back();
            s.error = JoinError::None;
            s.dirty = true;
        }
        else if (vk == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) paste(s);
        else if (const wchar_t ch = typed_char(vk)) type(s, std::wstring(1, ch));
        return true;
    }
    switch (vk) {
        case VK_ESCAPE:
            if (s.kick_asked >= 0) {  // not sending them away after all
                s.kick_asked = -1;
                s.dirty = true;
                return true;
            }
            shutdown();
            return true;
        case VK_RETURN: primary(s); break;
        case '1': case '2': case '3': set_bonus(s, vk - '1'); break;
        case 'S': cycle_side(s, 1); break;
        case 'M':
            s.mods_view = !s.mods_view;
            s.dirty = true;
            break;
        case VK_NUMPAD1: case VK_NUMPAD2: case VK_NUMPAD3: set_bonus(s, vk - VK_NUMPAD1); break;
        case VK_UP: case VK_LEFT: case VK_DOWN: case VK_RIGHT:
            if (s.mods_view) page_mods(s, vk == VK_UP || vk == VK_LEFT ? -1 : 1);
            else if (vk == VK_UP || vk == VK_DOWN) {
                if (s.mode == Mode::Host) move_map(s, vk == VK_UP ? -1 : 1);
            }
            else if (s.mode == Mode::Host) {
                const auto view = screen(s);
                s.page = std::clamp(s.page + (vk == VK_LEFT ? -1 : 1), 0, LobbyModel::page_count(view) - 1);
                s.dirty = true;
            }
            break;
        default: break;
    }
    return true;
}

bool mouse_down(int x, int y) {
    if (!active()) return false;
    State &s = *state;
    if (ModInfoWindow::open()) {  // any click closes a mod's window; a question takes its buttons only
        ModInfoWindow::click(x, y);
        return true;
    }
    x -= hint->m_PosX;
    y -= hint->m_PosY;
    if (s.mode == Mode::Name || s.mode == Mode::Join || s.asking_password) {  // drawn at the lobby's scale: hits in the layout's units
        const double scale = LobbyModel::scale_for(g_ScreenX, g_ScreenY);
        x = int(std::floor(x / scale));
        y = int(std::floor(y / scale));
    }
    if (s.asking_password) {
        const int action = LobbyModel::join_hit(x, y);
        if (action == 1) save_password(s);
        else if (action == -1) close_password(s);
        return true;
    }
    if (s.mode == Mode::Name) {
        const int action = LobbyModel::name_hit({s.language, s.name, s.name_forced}, x, y);
        if (action == 1) save_name(s);
        else if (action == -1) shutdown();
        return true;
    }
    if (s.mode == Mode::Join) {
        const int action = LobbyModel::join_hit(join_view(s), x, y);
        if (action >= 100 && std::size_t(action - 100) < s.rooms.size() &&
            s.client.phase() != NetLobby::Client::Phase::Connecting) {  // a room: its host
            const auto &room = s.rooms[std::size_t(action - 100)];
            s.typed = utils::to_wstring(room.address) + L":" + std::to_wstring(room.port);
            connect();
        }
        if (action == 1) connect();
        else if (action == -1) shutdown();
        return true;
    }
    const auto view = screen(s);
    // The lobby is drawn larger than its grid: the hit is in the grid's units.
    const auto target = LobbyModel::hit(view, int(std::floor(x / view.scale)), int(std::floor(y / view.scale)));
    if (target.person < 0 && !target.kick && s.kick_asked >= 0) {  // a click elsewhere: not asking any more
        s.kick_asked = -1;
        s.dirty = true;
    }
    if (target.action == 1) primary(s);
    else if (target.action == -1) shutdown();
    else if (target.action == 3) ask_host_password(s);
    else if (target.action == 2) {
        s.mods_view = !s.mods_view;
        s.dirty = true;
    }
    else if (view.mods_view && target.page) page_mods(s, target.page);
    else if (target.mod >= 0 && target.info) {
        ModInfoWindow::show(LobbyModel::mod_info(view, target.mod), s.language, view.scale);
    }
    else if (target.mod >= 0) {
        const auto rows = mod_rows(s);
        if (s.mode == Mode::Host && target.mod < int(rows.size())) {
            if (rows[std::size_t(target.mod)].kind == LobbyModel::ModRow::Kind::Battle) toggle_mod(s, target.mod);
            else if (rows[std::size_t(target.mod)].kind == LobbyModel::ModRow::Kind::Verified) {
                s.host.set_verified_only(!s.host.state().verified_only);
                lgr.notice("Lobby: only built-in personal mods {}")(s.host.state().verified_only ? "on" : "off");
                s.dirty = true;
            }
        }
    }
    else if (target.bonus >= 0) set_bonus(s, target.bonus);
    else if (target.side) cycle_side(s, target.side);
    else if (target.sort >= 0) set_sort(s, LobbyModel::MapSort(target.sort));
    else if (s.mode != Mode::Host) return true;
    else if (target.person >= 1) {
        s.kick_asked = target.person == s.kick_asked ? -1 : target.person;
        s.dirty = true;
        return true;
    }
    else if (target.kick) {
        if (target.kick > 0) s.host.kick(s.kick_asked);
        s.kick_asked = -1;
        s.dirty = true;
        return true;
    }
    else if (target.map >= 0) select_map(s, target.map);
    else if (target.page) {
        s.page = std::clamp(s.page + target.page, 0, LobbyModel::page_count(view) - 1);
        s.dirty = true;
    }
    else if (target.delay) {
        s.host.set_delay(Lobby::next_delay(s.host.state().delay, target.delay));
        s.dirty = true;
    }
    else if (target.ai) {
        s.host.set_ai_resources(std::clamp(s.host.state().ai_resources + target.ai, 0, BattleRules::max_ai_resources));
        s.dirty = true;
    }
    else if (target.manual) {
        const auto &st = s.host.state();
        if (target.manual == 1) s.host.set_manual(!st.manual_control, st.manual_bonus);
        else if (target.manual == 3) s.host.set_dropped_ai(!st.dropped_ai);
        else if (target.manual == 4) s.host.set_auto_resync(!st.auto_resync);
        else if (target.manual == 5) s.host.set_delay_follows(!st.delay_follows);
        else if (st.manual_control) s.host.set_manual(true, !st.manual_bonus);
        s.dirty = true;
    }
    return true;
}

void mouse_wheel(int steps, int x, int y) {
    if (!active() || (state->mode != Mode::Host && !state->mods_view) || ModInfoWindow::open()) return;
    (void)y;  // anywhere down the list's column
    const double scale = LobbyModel::scale_for(g_ScreenX, g_ScreenY);
    x = int(std::floor((x - hint->m_PosX) / scale));
    if (x < LobbyModel::Layout::list_x || x >= LobbyModel::Layout::list_x + LobbyModel::Layout::list_width) return;
    if (state->mods_view) page_mods(*state, steps > 0 ? -1 : 1);
    else move_map(*state, steps > 0 ? -1 : 1);
}

void reset_input() {
    if (state) state->actions = {};
}

void relayout() {
    if (active()) rebuild();
}

void shutdown() {
    ModInfoWindow::close();
    release_hint();
    MapPreview::release();
    if (state) {
        state->host.close();
        state->client.close();
        if (!battle_started) MapShare::forget();  // (a battle reads the host's map from its copy)
        // Back to the menu: the person's own display mode again.
        if (state->display_changed) {
            Display::set_exclusive_allowed(state->exclusive_was);
            if (state->exclusive_was)
                Display::request(state->values.display.mode, state->values.display.fullscreen);
        }
        state.reset();
    }
}

bool started() { return battle_started; }

std::wstring player_name(const NetBattle::Options &options) {
    return !name_saved && !options.lobby_name.empty() ? options.lobby_name : PlayerName::load();
}

}  // namespace LobbyWindow
