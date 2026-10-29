// MatrixGame - licensed under GPLv2 or any later version.
#include "LobbyWindow.hpp"
#include "LobbyModel.hpp"
#include "NetLobby.hpp"
#include "Lockstep.hpp"
#include "MapCatalog.hpp"
#include "MapShare.hpp"
#include "MapMetadata.hpp"
#include "MapPreview.hpp"
#include "MapSides.hpp"
#include "Mods.hpp"
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
#include <fstream>
#include <map>
#include <memory>
#include <optional>

namespace LobbyWindow {
namespace {

using LobbyModel::Language;
using LobbyModel::JoinError;

constexpr const wchar_t *address_file = L"CFG\\net-join.txt";  // the last host joined
constexpr const wchar_t *sort_file = L"CFG\\lobby-sort.txt";    // the map list's order: name or people
constexpr int connect_timeout_ms = 8000;
constexpr auto map_read_budget = std::chrono::milliseconds(15);

enum class Mode { Join, Host, Client, Name };

struct State {
    Mode mode = Mode::Join;
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
};

std::optional<State> state;
CMatrixHint *hint = nullptr;
bool battle_started = false;
bool name_saved = false;  // the person chose a name in this run: it wins over --lobby-name
std::map<std::wstring, int> sides_read;  // by map path, for the whole process
std::map<std::wstring, std::string> map_hashes;
std::map<std::wstring, std::vector<std::wstring>> map_lacks;  // MapPacks::missing, by map path, for the whole process
std::vector<std::unique_ptr<CBitmap>> lines;  // LobbyModel::separators(), made for lines_scale
double lines_scale = 0;
std::unique_ptr<CBitmap> toggles[2];  // a mod's switch, off and on, made for toggles_scale
double toggles_scale = 0;

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

// A mod's switch: a round button in a dark rim, lit blue when on, dark when off (a sphere's shading, smoothed edges).
std::unique_ptr<CBitmap> toggle(int size, bool on) {
    auto out = std::make_unique<CBitmap>();
    out->CreateRGBA(size, size);
    const float r = size * 0.5f, rim = std::max(1.5f, size * 0.12f);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float dx = x + 0.5f - r, dy = y + 0.5f - r, d = std::sqrt(dx * dx + dy * dy);
            const float alpha = std::clamp(r - d, 0.0f, 1.0f);  // the edge's coverage
            // The rim: metal, lighter at the top; inside: the light, brightest a little above the centre.
            float red, green, blue;
            if (d > r - rim) {
                const float k = 0.55f - 0.25f * dy / r;
                red = 70 * k + 30, green = 80 * k + 34, blue = 90 * k + 38;
            }
            else {
                const float hx = dx + r * 0.2f, hy = dy + r * 0.3f;
                const float t = std::clamp(1.0f - std::sqrt(hx * hx + hy * hy) / (r - rim), 0.0f, 1.0f);
                if (on) red = 20 + 200 * t * t, green = 110 + 140 * t, blue = 200 + 55 * t;
                else red = 38 + 40 * t * t, green = 42 + 44 * t * t, blue = 48 + 48 * t * t;
            }
            const uint32_t pixel = (uint32_t(alpha * 255) << 24) | (uint32_t(std::min(red, 255.0f)) << 16) |
                                   (uint32_t(std::min(green, 255.0f)) << 8) | uint32_t(std::min(blue, 255.0f));
            std::memcpy(out->Data() + y * out->Pitch() + x * 4, &pixel, 4);
        }
    return out;
}

void prepare_toggles(double scale) {
    if (toggles[0] && toggles_scale == scale) return;
    toggles_scale = scale;
    for (int on = 0; on < 2; ++on) {
        toggles[on] = toggle(LobbyModel::px(scale, LobbyModel::Layout::toggle_size), on != 0);
        CMatrixHint::SetRuntimeBitmap(LobbyModel::toggle_bitmap(on != 0), toggles[on].get());
    }
}

// The map file's fingerprint: the same name with other contents must not play together.
std::string map_hash(const std::wstring &path) {
    if (const auto known = map_hashes.find(path); known != map_hashes.end()) return known->second;
    // A copy the host gave; a map mod's map: its bundle (the map with the mod's files it takes).
    if (const auto kept = MapShare::used_hash(path)) return map_hashes[path] = *kept;
    if (MapPacks::of_map(MapPacks::installed(), path)) {
        const auto bundle = MapPacks::bundle(path);
        return map_hashes[path] = bundle ? MapShare::fingerprint(*bundle) : std::string();
    }
    std::string hash;
    try {
        CBuf data;
        data.LoadFromFile(path);
        hash = GameData::hex(GameData::hash_bytes(data.Get(), std::size_t(data.Len())));
    }
    catch (...) {
    }
    map_hashes[path] = hash;
    return hash;
}

Language language() {
    return SettingsModel::detect_language(g_MatrixData->BlockGet(PAR_REPLACE)->ParGetNE(L"setup"));
}

void load_maps(State &s) {
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
    const auto metadata = MapMetadata::load(diagnostic);
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
        rows.push_back(std::move(row));
    }
    if (s.mode != Mode::Host)
        for (const Lobby::ModRef &r : st.mods) {
            const Mods::Mod *m = Mods::find(Mods::installed(), r.id);
            if (!m || m->kind != Mods::Kind::Battle)
                rows.push_back({utils::to_wstring(r.id), {}, true, LobbyModel::ModRow::Problem::Missing});
        }
    return rows;
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
    view.mods = mod_rows(s);
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
    view.address = s.host_address;
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

void rebuild() {
    State &s = *state;
    std::wstring markup;
    if (s.mode == Mode::Name) {
        markup = LobbyModel::name_template({s.language, s.name, s.name_forced, LobbyModel::scale_for(g_ScreenX, g_ScreenY)});
    }
    else if (s.mode == Mode::Join) {
        MapPreview::release();
        markup = LobbyModel::join_template(
            {s.language, s.typed, s.client.phase() == NetLobby::Client::Phase::Connecting, s.error,
             LobbyModel::scale_for(g_ScreenX, g_ScreenY)});
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
    s.dirty = false;
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
    std::ofstream out{std::filesystem::path(address_file), std::ios::trunc};
    out << utils::from_wstring(text) << '\n';
}

// Ends the startup menu; the game loads the prepared battle.
void finish() {
    battle_started = true;
    if (!BattleLoading::show(g_Wnd, state->language == Language::Russian))
        lgr.warning("Cannot create the loading screen; continuing battle initialization");
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
    // Back from a battle the host may still be reading the statistics: wait for its lobby up to ten
    // minutes (Back stops waiting).
    s.client.connect(endpoint->address, endpoint->port, s.bonus, s.options.lobby_return ? 600000 : connect_timeout_ms,
                     GameData::data_fingerprint(),
                     GameData::build_fingerprint(), own_name(s));
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

// The host switches a battle mod (the row's place among the installed battle mods) and keeps the choice for its
// next lobbies and own battles.
void toggle_mod(State &s, int row) {
    std::vector<std::string> ids;
    for (const auto &r : s.host.state().mods) ids.push_back(r.id);
    int index = 0;
    for (const Mods::Mod &m : Mods::installed()) {
        if (m.kind != Mods::Kind::Battle) continue;
        if (index++ != row) continue;
        if (const auto at = std::find(ids.begin(), ids.end(), m.id); at != ids.end()) ids.erase(at);
        else ids.push_back(m.id);
    }
    s.host.set_mods(mod_refs(ids));
    s.values.mods = ids;
    std::string error;
    if (!StandaloneSettings::save(s.values, error)) lgr.warning("Lobby: cannot keep the mods chosen: {}")(error);
    lgr.notice("Lobby: battle mods {}")(Mods::join(ids));
    s.dirty = true;
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
        return;
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

void type(State &s, const std::wstring &text) {
    if (s.mode == Mode::Name) {
        for (wchar_t ch : text)
            if (PlayerName::valid_char(ch) && s.name.size() < PlayerName::max_length) s.name += ch;
        s.dirty = true;
        return;
    }
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
    if (!s.host.open(address, port, s.bonus, GameData::data_fingerprint(), GameData::build_fingerprint(), own_name(s))) {
        lgr.error("Lobby: cannot listen on {}:{}")(address, port);
        state.reset();
        return false;
    }
    lgr.notice("Lobby: hosting on {}:{}")(address, port);
    s.listen_address = address;
    s.listen_port = port;
    if (address == "0.0.0.0") {
        // Every address of this PC (a LAN's, a VPN's or a virtual LAN's), a line each in the lobby.
        for (const auto &a : NetLink::local_addresses()) {
            if (std::count(s.host_address.begin(), s.host_address.end(), L',') >= 3) break;
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
    rebuild();
    return true;
}

void open_local(const StandaloneSettings::Values &values, const NetBattle::Options &options) {
    open(Mode::Host, values, options, true);
    State &s = *state;
    s.host.open_local(s.bonus, own_name(s));
    lgr.notice("Lobby: a battle against the AI");
    first_choices(s, values, options);
    rebuild();
}

void open_join(const StandaloneSettings::Values &values, const NetBattle::Options &options) {
    open(Mode::Join, values, options);
    State &s = *state;
    s.typed = options.lobby ? utils::to_wstring(options.address) + L":" + std::to_wstring(options.port) : saved_address();
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
    // By people, rows move while the maps are read: the host's page follows the lobby's map.
    if (reading && map_sort == LobbyModel::MapSort::People && s.mode == Mode::Host) page_to_selected(s);
    if (s.dirty) rebuild();
}

bool keyboard(uint8_t vk, bool down) {
    if (!active()) return false;
    State &s = *state;
    if (!s.actions.accept(vk, down)) return true;
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
        case VK_UP: if (s.mode == Mode::Host) move_map(s, -1); break;
        case VK_DOWN: if (s.mode == Mode::Host) move_map(s, 1); break;
        case VK_LEFT: case VK_RIGHT:
            if (s.mode == Mode::Host) {
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
    x -= hint->m_PosX;
    y -= hint->m_PosY;
    if (s.mode == Mode::Name || s.mode == Mode::Join) {  // drawn at the lobby's scale: hits in the layout's units
        const double scale = LobbyModel::scale_for(g_ScreenX, g_ScreenY);
        x = int(std::floor(x / scale));
        y = int(std::floor(y / scale));
    }
    if (s.mode == Mode::Name) {
        const int action = LobbyModel::name_hit({s.language, s.name, s.name_forced}, x, y);
        if (action == 1) save_name(s);
        else if (action == -1) shutdown();
        return true;
    }
    if (s.mode == Mode::Join) {
        const int action = LobbyModel::join_hit(x, y);
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
    else if (target.action == 2) {
        s.mods_view = !s.mods_view;
        s.dirty = true;
    }
    else if (view.mods_view && target.page) {
        s.mods_page = std::clamp(view.mods_page + target.page, 0, LobbyModel::mods_page_count(view) - 1);
        s.dirty = true;
    }
    else if (target.mod >= 0) {
        if (s.mode == Mode::Host) toggle_mod(s, target.mod);
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
        else if (st.manual_control) s.host.set_manual(true, !st.manual_bonus);
        s.dirty = true;
    }
    return true;
}

void mouse_wheel(int steps, int x, int y) {
    if (!active() || state->mode != Mode::Host) return;
    (void)y;  // anywhere down the list's column
    const double scale = LobbyModel::scale_for(g_ScreenX, g_ScreenY);
    x = int(std::floor((x - hint->m_PosX) / scale));
    if (x < LobbyModel::Layout::list_x || x >= LobbyModel::Layout::list_x + LobbyModel::Layout::list_width) return;
    move_map(*state, steps > 0 ? -1 : 1);
}

void reset_input() {
    if (state) state->actions = {};
}

void relayout() {
    if (active()) rebuild();
}

void shutdown() {
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
