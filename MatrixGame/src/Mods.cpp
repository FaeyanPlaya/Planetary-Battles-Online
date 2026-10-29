// MatrixGame - licensed under GPLv2 or any later version.
#include "Mods.hpp"

#include "GameData.hpp"

#include "utils.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>

namespace Mods {
namespace {

Switches g_switches;
float g_far_view_scale = 2.0f;

struct SwitchName {
    const char *name;
    bool Switches::*flag;
};
// Every switch a battle mod may turn on (the battle reads them on every peer alike).
constexpr SwitchName battle_switches[] = {
    {"liftsight", &Switches::lift_sight},
    {"capturelift", &Switches::capture_lift},
    {"drivereachreset", &Switches::drive_reach_reset},
    {"capturenotimeout", &Switches::capture_no_timeout},
    {"stopinplace", &Switches::stop_in_place},
    {"repairplant", &Switches::repair_plant},
    {"turretcontrol", &Switches::turret_control},
};
// Every switch a personal mod may turn on: what this person sees and how their input becomes commands.
constexpr SwitchName personal_switches[] = {
    {"driveaimfar", &Switches::drive_aim_far},
    {"farview", &Switches::far_view},
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

Mod made(std::string id, const wchar_t *name, const wchar_t *name_en, const wchar_t *description,
         const wchar_t *description_en, Kind kind, std::vector<std::string> switches) {
    Mod mod{std::move(id), name, name_en, description, description_en, kind, true, std::move(switches), {}, {}};
    std::uint64_t h = GameData::hash_bytes("pbo-builtin", 11);
    h = GameData::hash_bytes(mod.id.data(), mod.id.size() + 1, h);
    for (const auto &s : mod.switches) h = GameData::hash_bytes(s.data(), s.size() + 1, h);
    mod.hash = GameData::hex(h);
    return mod;
}

// The project's mods, all on for a person who never chose. Switches in lower case.
std::vector<Mod> builtin_mods() {
    return {
        // FarView: the far clip plane and the fog, the distance effects are made at and how many of each kind are
        //   kept, all times the scale (FarViewScale=, 1.5 to 3 times, 2 by default); shadows drawn to 1536 times it
        //   instead of 1024. Drawing only: the battle never reads it.
        made("farview", L"\u0414\u0430\u043b\u044c\u043d\u044f\u044f \u043f\u0440\u043e\u0440\u0438\u0441\u043e\u0432\u043a\u0430", L"Far view",
             L"\u0422\u0443\u043c\u0430\u043d \u0432\u0434\u0432\u043e\u0435 \u0434\u0430\u043b\u044c\u0448\u0435, \u044d\u0444\u0444\u0435\u043a\u0442\u044b \u0438 \u0442\u0435\u043d\u0438 "
             L"\u0432\u0438\u0434\u043d\u044b \u0434\u0430\u043b\u044c\u0448\u0435",
             L"Fog twice as far; effects and shadows seen farther", Kind::Personal, {"farview"}),
        // CaptureNoTimeout: the 10 s limit of a robot with a base (for a new robot stuck on its lift) no longer kills
        //   a robot whose capture is held up (the original killed it and closed the base).
        // StopInPlace: Stop halts each robot where it stands (the original sent the group on to free places round
        //   its centre).
        made("fixes", L"\u0418\u0441\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u044f \u0431\u043e\u044f", L"Battle fixes",
             L"\u0417\u0430\u0445\u0432\u0430\u0442\u0447\u0438\u043a \u0431\u0430\u0437\u044b \u043d\u0435 \u043f\u043e\u0433\u0438\u0431\u0430\u0435\u0442 \u0447\u0435\u0440\u0435\u0437 10 \u0441; "
             L"\u043f\u043e \u043a\u043e\u043c\u0430\u043d\u0434\u0435 \u0421\u0442\u043e\u043f \u0440\u043e\u0431\u043e\u0442\u044b \u0432\u0441\u0442\u0430\u044e\u0442 \u043d\u0430 \u043c\u0435\u0441\u0442\u0435",
             L"A base's capturer does not die after 10 s; Stop halts robots where they stand", Kind::Battle,
             {"capturenotimeout", "stopinplace"}),
        // DriveAimFar: over the sky or water past the map's edge the cursor's trace finds nothing and the original
        //   kept the robot aiming at the last point it found; the robot now aims along the cursor's ray.
        made("interface", L"\u0418\u0441\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u044f \u0438\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441\u0430", L"Interface fixes",
             L"\u0412 \u0440\u0443\u0447\u043d\u043e\u043c \u0443\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u0438 \u0440\u043e\u0431\u043e\u0442 \u0446\u0435\u043b\u0438\u0442\u0441\u044f \u0432 \u043d\u0435\u0431\u043e \u0438 \u0432 \u0432\u043e\u0434\u0443 "
             L"\u0437\u0430 \u043a\u0440\u0430\u0435\u043c \u043a\u0430\u0440\u0442\u044b",
             L"A driven robot aims at the sky and at water past the map's edge", Kind::Personal, {"driveaimfar"}),
        // LiftSight: robots on a base's lift see the enemies in reach and are seen (the original ignored them there).
        // CaptureLift: a capturer rides the lift down when something else closed it (it stood in the air).
        // DriveReachReset: a robot its driver left loses the reach bonus too (the original kept it).
        made("pbo", L"Planetary Battles Online", L"Planetary Battles Online",
             L"\u041d\u0430 \u043b\u0438\u0444\u0442\u0435 \u0431\u0430\u0437\u044b \u0440\u043e\u0431\u043e\u0442\u044b \u0432\u0438\u0434\u044f\u0442 \u0432\u0440\u0430\u0433\u043e\u0432, "
             L"\u0437\u0430\u0445\u0432\u0430\u0442\u0447\u0438\u043a \u0435\u0434\u0435\u0442 \u0432\u043d\u0438\u0437; \u0431\u0440\u043e\u0448\u0435\u043d\u043d\u044b\u0439 \u0440\u043e\u0431\u043e\u0442 \u0442\u0435\u0440\u044f\u0435\u0442 \u0431\u043e\u043d\u0443\u0441",
             L"Robots on a base lift see enemies, a capturer rides it down; a robot left loses its bonus", Kind::Battle,
             {"liftsight", "capturelift", "drivereachreset"}),
        // RepairPlant: a repair factory of a side keeps two repairer's beams from its probes, at the rate of a robot
        //   with four repairers, on the nearest damaged robot of that side standing in its capture area; the
        //   reinforcements' lift field reaches from its hatch to the robot meanwhile. A captured factory repairs for
        //   its new side; a neutral one does nothing.
        made("repair", L"\u0420\u0435\u043c\u043e\u043d\u0442\u043d\u044b\u0435 \u0437\u0430\u0432\u043e\u0434\u044b", L"Repair factories",
             L"\u0420\u0435\u043c\u043e\u043d\u0442\u043d\u044b\u0439 \u0437\u0430\u0432\u043e\u0434 \u0447\u0438\u043d\u0438\u0442 \u0440\u043e\u0431\u043e\u0442\u043e\u0432 \u0441\u0432\u043e\u0435\u0439 \u0441\u0442\u043e\u0440\u043e\u043d\u044b "
             L"\u0432 \u0437\u043e\u043d\u0435 \u0437\u0430\u0445\u0432\u0430\u0442\u0430",
             L"A repair factory repairs its side's robots in its capture area", Kind::Battle, {"repairplant"}),
        // TurretControl: a person selects their side's turret with a click. An order to attack a unit (a robot, a
        //   flyer or a turret) makes the turret fire at it while it stays in the turret's reach, then the turret
        //   chooses its targets again. A person enters a built turret as a robot: the turret turns to the cursor at
        //   its own speed within its own angles and fires while the button is held; the lobby's manual control bonus
        //   lengthens its reach as a robot's. Dismantling a built turret frees its place and returns half its price,
        //   times the share of its hit points left.
        made("turrets", L"\u0423\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u0435 \u0442\u0443\u0440\u0435\u043b\u044f\u043c\u0438", L"Turret control",
             L"\u0421\u0432\u043e\u0438 \u0442\u0443\u0440\u0435\u043b\u0438 \u043c\u043e\u0436\u043d\u043e \u0432\u044b\u0434\u0435\u043b\u044f\u0442\u044c, \u043f\u0440\u0438\u043a\u0430\u0437\u044b\u0432\u0430\u0442\u044c \u0438\u043c \u0446\u0435\u043b\u044c, "
             L"\u0443\u043f\u0440\u0430\u0432\u043b\u044f\u0442\u044c \u0432\u0440\u0443\u0447\u043d\u0443\u044e \u0438 \u0440\u0430\u0437\u0431\u0438\u0440\u0430\u0442\u044c",
             L"A side's turrets can be selected, given a target, driven by hand and dismantled", Kind::Battle,
             {"turretcontrol"}),
    };
}

}  // namespace

const Switches &switches() { return g_switches; }

void set_switches(const Switches &switches) { g_switches = switches; }

void set_personal(const std::vector<std::string> &personal, float far_view_scale) {
    Switches on = g_switches;
    for (const auto &known : personal_switches) on.*known.flag = false;
    collect_personal(installed(), personal, on);
    g_switches = on;
    g_far_view_scale = std::clamp(far_view_scale, 1.0f, 3.0f);
}

float view_scale() { return g_switches.far_view ? g_far_view_scale : 1.0f; }

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

Mod parse(std::wstring_view text) {
    Mod mod;
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
        else if (key == L"description") mod.description = value;
        else if (key == L"descriptionen") mod.description_en = value;
        else if (key == L"kind") mod.kind = lower(value) == L"personal" ? Kind::Personal : Kind::Battle;
        else if (key == L"default") mod.default_on = lower(value) == L"on";
        else if (key == L"switches") {
            std::wstring_view list = value;
            while (!list.empty()) {
                const auto comma = list.find(L',');
                const std::wstring item = utils::trim(std::wstring(list.substr(0, comma)));
                list = comma == std::wstring_view::npos ? std::wstring_view{} : list.substr(comma + 1);
                if (!item.empty()) mod.switches.push_back(lower_ascii(utils::from_wstring(item)));
            }
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
        mod.folder = it->path();
        mod.hash = folder_hash(it->path());
        mods.push_back(std::move(mod));
    }
    std::sort(mods.begin(), mods.end(), [](const Mod &a, const Mod &b) { return a.id < b.id; });
    return mods;
}

const std::vector<Mod> &builtin() {
    static const std::vector<Mod> mods = builtin_mods();
    return mods;
}

std::vector<Mod> catalogue(const std::vector<Mod> &scanned) {
    std::vector<Mod> mods = builtin();
    for (const Mod &m : scanned)
        if (!find(builtin(), m.id)) mods.push_back(m);
    std::sort(mods.begin(), mods.end(), [](const Mod &a, const Mod &b) { return a.id < b.id; });
    return mods;
}

const std::vector<Mod> &installed() {
    static const std::vector<Mod> mods = catalogue({});
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
        if (!mod || mod->kind != Kind::Battle) {
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
        if (!mod || mod->kind != Kind::Personal) {
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
    return true;
}

}  // namespace Mods
