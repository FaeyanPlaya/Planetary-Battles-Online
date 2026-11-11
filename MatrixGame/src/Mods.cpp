// MatrixGame - licensed under GPLv2 or any later version.
#include "Mods.hpp"
#include "ModHooks.hpp"

#include "GameData.hpp"
#include "KeyBindings.hpp"

#include "utils.hpp"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>

namespace Mods {
namespace {

Switches g_switches;
float g_far_view_scale = 2.0f;
std::vector<std::string> g_battle_on, g_personal_on;  // the mods on: the battle's, this person's
bool g_verified_only = false;

struct SwitchName {
    const char *name;
    bool Switches::*flag;
};
// Every switch a battle mod may turn on (the battle reads them on every peer alike).
constexpr SwitchName battle_switches[] = {
    {"liftsight", &Switches::lift_sight},
    {"capturelift", &Switches::capture_lift},
    {"drivereachreset", &Switches::drive_reach_reset},
    {"turretplaces", &Switches::turret_places},
    {"capturestay", &Switches::capture_stay},
    {"capturenotimeout", &Switches::capture_no_timeout},
    {"stopinplace", &Switches::stop_in_place},
    {"repairplant", &Switches::repair_plant},
    {"turretcontrol", &Switches::turret_control},
    {"autoboom", &Switches::auto_boom},
    {"homingmissiles", &Switches::homing_missiles},
    {"missileballistics", &Switches::missile_ballistics},
    {"rallypoint", &Switches::rally_point},
    {"manualextras", &Switches::manual_extras},
};
// Every switch a personal mod may turn on: what this person sees and how their input becomes commands.
constexpr SwitchName personal_switches[] = {
    {"driveaimfar", &Switches::drive_aim_far},
    {"turretlaserglow", &Switches::turret_laser_glow},
    {"dialogesc", &Switches::dialog_esc},
    {"minimapcursor", &Switches::minimap_cursor},
    {"buildinggroups", &Switches::building_groups},
    {"constructorkeys", &Switches::constructor_keys},
    {"zoomfar", &Switches::zoom_far},
    {"camerashift", &Switches::camera_shift},
    {"chassismotion", &Switches::chassis_motion},
    {"farview", &Switches::far_view},
    {"firstperson", &Switches::first_person},
    {"newsky", &Switches::new_sky},
};

std::string lower_ascii(std::string_view text) {
    std::string out(text);
    for (char &ch : out)
        if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
    return out;
}

std::wstring lower(std::wstring text) {
    for (wchar_t &ch : text)
        if (ch >= L'A' && ch <= L'Z') ch = wchar_t(ch - L'A' + L'a');
    return text;
}

std::optional<std::string> file_bytes(const std::filesystem::path &file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), {});
}

}  // namespace

// Every file under the folder, by its path from there in lower case with '/': names and bytes.
std::string folder_hash(const std::filesystem::path &folder) {
    std::vector<std::pair<std::string, std::filesystem::path>> files;
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        std::string name = lower_ascii(utils::from_wstring(std::filesystem::relative(it->path(), folder, error).generic_wstring()));
        files.emplace_back(std::move(name), it->path());
    }
    std::sort(files.begin(), files.end());
    std::uint64_t h = GameData::hash_bytes("pbo-mod", 7);
    for (const auto &[name, path] : files) {
        h = GameData::hash_bytes(name.data(), name.size() + 1, h);  // with the terminating zero
        const auto bytes = file_bytes(path);
        h = bytes ? GameData::hash_bytes(bytes->data(), bytes->size(), h) : GameData::hash_bytes("?", 1, h);
    }
    return GameData::hex(h);
}

const Switches &switches() { return g_switches; }

void set_switches(const Switches &switches) { g_switches = switches; }

void set_on(const std::vector<std::string> &battle, const std::vector<std::string> &personal) {
    g_battle_on = battle;
    g_personal_on = personal;
}

void set_personal(const std::vector<std::string> &personal, float far_view_scale) {
    Switches on = g_switches;
    for (const auto &known : personal_switches) on.*known.flag = false;
    const auto absent = collect_personal(installed(), personal, on);
    g_switches = on;
    g_personal_on.clear();
    for (const auto &id : personal)
        if (std::find(absent.begin(), absent.end(), id) == absent.end()) g_personal_on.push_back(id);
    g_far_view_scale = std::clamp(far_view_scale, 1.0f, 3.0f);
}

float view_scale() {
    const auto hook = ModHooks::hooks().view_scale;  // the far view mod's
    return g_switches.far_view && hook ? hook(g_far_view_scale) : 1.0f;
}

std::string id_of(std::wstring_view folder_name) {
    if (folder_name.empty() || folder_name.size() > 32) return {};
    std::string id;
    for (wchar_t ch : folder_name) {
        if (ch >= L'A' && ch <= L'Z') ch = wchar_t(ch - L'A' + L'a');
        if (!((ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') || ch == L'_' || ch == L'-')) return {};
        id += char(ch);
    }
    return id;
}

std::wstring decode(std::string_view bytes) {
    if (bytes.size() >= 2 && bytes[0] == '\xff' && bytes[1] == '\xfe') {
        std::wstring text;
        for (std::size_t i = 2; i + 1 < bytes.size(); i += 2)
            text += wchar_t(std::uint8_t(bytes[i]) | (std::uint8_t(bytes[i + 1]) << 8));
        return text;
    }
    if (bytes.starts_with("\xef\xbb\xbf")) bytes.remove_prefix(3);
    try {
        return utils::to_wstring(bytes);
    }
    catch (...) {
        return {};
    }
}

namespace {
// "a, B ,c" -> a, B, c, each trimmed, the empty ones left out.
std::vector<std::wstring> split_on(std::wstring_view list, wchar_t separator, bool keep_empty = false) {
    std::vector<std::wstring> items;
    while (true) {
        const auto at = list.find(separator);
        std::wstring item = utils::trim(std::wstring(list.substr(0, at)));
        if (keep_empty || !item.empty()) items.push_back(std::move(item));
        if (at == std::wstring_view::npos) break;
        list = list.substr(at + 1);
    }
    return items;
}

// "a, B,c" -> a, b, c.
std::vector<std::string> list_of(std::wstring_view list) {
    std::vector<std::string> items;
    for (const auto &item : split_on(list, L',')) items.push_back(lower_ascii(utils::from_wstring(item)));
    return items;
}

// Key.<Id>, Setting.<Id>: <Id> of letters, digits and _ (empty when it is not one).
std::string line_id(const std::wstring &key, std::size_t prefix) {
    const std::wstring id = key.substr(prefix);
    if (id.empty()) return {};
    for (wchar_t c : id)
        if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_'))
            return {};
    return utils::from_wstring(id);
}

std::optional<KeyLine> key_line(const std::wstring &key, const std::wstring &value) {
    KeyLine line;
    line.id = line_id(key, 4);
    auto parts = split_on(value, L'|', true);
    if (line.id.empty() || parts.size() < 2) return std::nullopt;
    parts.resize(4);
    line.key = parts[0];
    line.russian = parts[1];
    line.english = parts[2];
    line.group = parts[3];
    return line;
}

std::optional<SettingLine> setting_line(const std::wstring &key, const std::wstring &value) {
    SettingLine line;
    line.id = line_id(key, 8);
    auto parts = split_on(value, L'|', true);
    if (line.id.empty() || parts.size() < 3) return std::nullopt;
    parts.resize(4);
    line.values = split_on(parts[0], L',');
    if (line.values.empty()) return std::nullopt;
    line.default_value = std::find(line.values.begin(), line.values.end(), parts[1]) != line.values.end() ? parts[1]
                                                                                                         : line.values[0];
    line.russian = parts[2];
    line.english = parts[3];
    return line;
}
}  // namespace

Mod parse(std::wstring_view text) {
    Mod mod;
    bool described = false, described_en = false;
    while (!text.empty()) {
        const auto end = text.find(L'\n');
        std::wstring line(text.substr(0, end));
        text = end == std::wstring_view::npos ? std::wstring_view{} : text.substr(end + 1);
        line = utils::trim(line);
        if (line.empty() || line.starts_with(L"#") || line.starts_with(L"//")) continue;
        const auto eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        const std::wstring key = lower(utils::trim(line.substr(0, eq)));
        const std::wstring value = utils::trim(line.substr(eq + 1));
        if (key == L"name") mod.name = value;
        else if (key == L"nameen") mod.name_en = value;
        else if (key == L"description") mod.description += (described ? L"\n" : L"") + value, described = true;
        else if (key == L"descriptionen") mod.description_en += (described_en ? L"\n" : L"") + value, described_en = true;
        else if (key == L"author") mod.author = value;
        else if (key == L"kind") mod.kind = lower(value) == L"personal" ? Kind::Personal : Kind::Battle;
        else if (key == L"default") mod.default_on = lower(value) == L"on";
        else if (key == L"dll") mod.dll = value;
        else if (key == L"switches") mod.switches = list_of(value);
        else if (key == L"dependence") mod.depends = list_of(value);
        else if (key == L"conflict") mod.conflicts = list_of(value);
        else if (key.starts_with(L"key.")) {
            if (auto parsed = key_line(utils::trim(line.substr(0, eq)), value)) mod.keys.push_back(std::move(*parsed));
        }
        else if (key.starts_with(L"setting.")) {
            if (auto parsed = setting_line(utils::trim(line.substr(0, eq)), value)) mod.settings.push_back(std::move(*parsed));
        }
    }
    return mod;
}

std::vector<Mod> scan(const std::filesystem::path &folder) {
    std::vector<Mod> mods;
    std::error_code error;
    for (auto it = std::filesystem::directory_iterator(folder, error);
         !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
        if (!it->is_directory(error)) continue;
        const std::string id = id_of(it->path().filename().wstring());
        const auto bytes = file_bytes(it->path() / L"mod.txt");
        if (id.empty() || !bytes || find(mods, id)) continue;
        Mod mod = parse(decode(*bytes));
        mod.id = id;
        if (mod.name.empty()) mod.name = utils::to_wstring(id);
        if (mod.name_en.empty()) mod.name_en = mod.name;
        if (mod.description_en.empty()) mod.description_en = mod.description;
        // Shown through the interface's text templates, where '|' ends a text: another's mod.txt must not break the
        // lists it is shown in.
        for (std::wstring *text : {&mod.name, &mod.name_en, &mod.description, &mod.description_en, &mod.author})
            std::replace(text->begin(), text->end(), L'|', L'/');
        mod.folder = it->path();
        mod.hash = folder_hash(it->path());
        mods.push_back(std::move(mod));
    }
    std::sort(mods.begin(), mods.end(), [](const Mod &a, const Mod &b) { return a.id < b.id; });
    return mods;
}

namespace {
// The keys of the mods that can be used, for KeyBindings.
std::vector<KeyBindings::ModAction> key_actions(const std::vector<Mod> &mods) {
    std::vector<KeyBindings::ModAction> actions;
    for (const Mod &mod : mods) {
        if (mod.problem != Problem::None) continue;
        for (const KeyLine &key : mod.keys) {
            KeyBindings::ModAction action;
            action.id = mod.id + "." + key.id;
            action.russian = key.russian;
            action.english = key.english.empty() ? key.russian : key.english;
            action.group = KeyBindings::group_of(key.group);
            action.key = KeyBindings::code(key.key);
            actions.push_back(std::move(action));
        }
    }
    return actions;
}
}  // namespace

const std::vector<Mod> &installed() {
    static const std::vector<Mod> mods = [] {
        std::vector<Mod> all = scan(folder);
        load_code(all);
        KeyBindings::set_mod_actions(key_actions(all));
        return all;
    }();
    return mods;
}

const Mod *find(const std::vector<Mod> &mods, std::string_view id) {
    const auto it = std::find_if(mods.begin(), mods.end(), [&](const Mod &m) { return m.id == id; });
    return it == mods.end() ? nullptr : &*it;
}

std::vector<std::string> default_battle(const std::vector<Mod> &mods) {
    std::vector<std::string> ids;
    for (const Mod &m : mods)
        if (m.kind == Kind::Battle && m.default_on) ids.push_back(m.id);
    return ids;
}

std::vector<std::string> default_personal(const std::vector<Mod> &mods) {
    std::vector<std::string> ids;
    for (const Mod &m : mods)
        if (m.kind == Kind::Personal && m.default_on) ids.push_back(m.id);
    return ids;
}

std::string join(const std::vector<std::string> &ids) {
    std::string text;
    for (const auto &id : ids) text += (text.empty() ? "" : ",") + id;
    return text.empty() ? std::string("-") : text;
}

std::vector<std::string> split(std::string_view text) {
    std::vector<std::string> ids;
    if (text == "-") return ids;
    while (!text.empty()) {
        const auto comma = text.find(',');
        const auto item = text.substr(0, comma);
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
        if (!item.empty() && std::find(ids.begin(), ids.end(), item) == ids.end()) ids.emplace_back(item);
    }
    return ids;
}

std::vector<std::string> collect(const std::vector<Mod> &mods, const std::vector<std::string> &battle, Switches &out) {
    std::vector<std::string> missing;
    out = {};
    for (const auto &id : battle) {
        const Mod *mod = find(mods, id);
        if (!mod || mod->kind != Kind::Battle || mod->problem != Problem::None) {
            missing.push_back(id);
            continue;
        }
        for (const auto &name : mod->switches)
            for (const auto &known : battle_switches)
                if (name == known.name) out.*known.flag = true;
    }
    return missing;
}

std::vector<std::string> collect_personal(const std::vector<Mod> &mods, const std::vector<std::string> &personal,
                                          Switches &out) {
    std::vector<std::string> missing;
    for (const auto &id : personal) {
        const Mod *mod = find(mods, id);
        if (!mod || mod->kind != Kind::Personal || mod->problem != Problem::None ||
            (g_verified_only && !verified(mod->id, mod->hash))) {
            missing.push_back(id);
            continue;
        }
        for (const auto &name : mod->switches)
            for (const auto &known : personal_switches)
                if (name == known.name) out.*known.flag = true;
    }
    return missing;
}

bool apply(const std::vector<std::string> &battle, const std::vector<std::string> &personal, std::string &missing,
           std::string &skipped) {
    Switches on;
    const auto absent = collect(installed(), battle, on);
    if (!absent.empty()) {
        missing = join(absent);
        return false;
    }
    const auto left_out = collect_personal(installed(), personal, on);
    skipped = left_out.empty() ? std::string() : join(left_out);
    g_switches = on;
    g_battle_on = battle;
    g_personal_on.clear();
    for (const auto &id : personal)
        if (std::find(left_out.begin(), left_out.end(), id) == left_out.end()) g_personal_on.push_back(id);
    return true;
}

bool on(std::string_view id) {
    return std::find(g_battle_on.begin(), g_battle_on.end(), id) != g_battle_on.end() ||
           std::find(g_personal_on.begin(), g_personal_on.end(), id) != g_personal_on.end();
}

bool verified(std::string_view id, std::string_view hash) {
    const auto *list = reinterpret_cast<const char *>(GetProcAddress(GetModuleHandleW(nullptr), "pbo_verified_mods"));
    if (!list || id.empty() || hash.empty()) return false;
    const std::string entry = std::string(id) + ":" + std::string(hash) + ";";
    const std::string_view all(list);
    for (std::size_t at = 0; (at = all.find(entry, at)) != std::string_view::npos; ++at)
        if (at == 0 || all[at - 1] == ';') return true;
    return false;
}

void set_verified_only(bool only) { g_verified_only = only; }

namespace {
std::optional<SettingValues> g_setting_values;
}

SettingValues load_settings(const std::filesystem::path &path) {
    SettingValues values;
    const auto bytes = file_bytes(path);
    if (!bytes) return values;
    for (const auto &line : split_on(decode(*bytes), L'\n')) {
        if (line.starts_with(L";") || line.starts_with(L"#")) continue;
        const auto eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        const std::string id = utils::from_wstring(utils::trim(line.substr(0, eq)));
        const auto dot = id.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 == id.size()) continue;
        values[id] = utils::trim(line.substr(eq + 1));
    }
    return values;
}

bool save_settings(const std::filesystem::path &path, const SettingValues &values, std::string &error) {
    std::string text = "; Planetary Battles Online: this person's settings of mods (the settings' Mods page)\n";
    for (const auto &[id, value] : values) text += id + "=" + utils::from_wstring(value) + "\n";
    auto temporary = path;
    temporary += L".tmp";
    std::error_code failed;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), failed);
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream.write(text.data(), std::streamsize(text.size()));
        if (!stream.good()) {
            error = "cannot write a temporary file";
            return false;
        }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "cannot replace the file";
        std::filesystem::remove(temporary, failed);
        return false;
    }
    return true;
}

const SettingValues &setting_values() {
    if (!g_setting_values) g_setting_values = load_settings(settings_file);
    return *g_setting_values;
}

void set_setting_values(SettingValues values) { g_setting_values = std::move(values); }

}  // namespace Mods
