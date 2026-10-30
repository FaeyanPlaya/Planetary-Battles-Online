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
    {"autoboom", &Switches::auto_boom},
    {"homingmissiles", &Switches::homing_missiles},
    {"missileballistics", &Switches::missile_ballistics},
    {"sr2balance", &Switches::sr2_balance},
    {"helpfromabove", &Switches::help_from_above},
    {"rallypoint", &Switches::rally_point},
};
// Every switch a personal mod may turn on: what this person sees and how their input becomes commands.
constexpr SwitchName personal_switches[] = {
    {"driveaimfar", &Switches::drive_aim_far},
    {"turretlaserglow", &Switches::turret_laser_glow},
    {"dialogesc", &Switches::dialog_esc},
    {"minimapcursor", &Switches::minimap_cursor},
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
        // AutoBoom: a robot of a person's side with a bomb sets it off as it dies (the original did it only for
        //   the AI's robots, and only with more enemy than own strength in the blast). Each robot keeps its own
        //   choice, on when built; a right click on the bomb order turns it for the selected bombers (a command).
        made("autoboom", L"Автоподрыв", L"Auto detonation",
             L"Робот с бомбой взрывает её, когда погибает; "
             L"правый щелчок по кнопке бомбы выключает",
             L"A robot with a bomb sets it off as it dies; a right click on the bomb order turns it off", Kind::Battle,
             {"autoboom"}),
        // MissileBallistics: a missile's (a robot's or a missile turret's) engine burns out when it is as far from
        //   where it was fired as its weapon reaches; it then goes on unpowered, slowed by the air and pulled down,
        //   and nothing steers it. It hurts whatever it hits at any distance (the original hurt only within the
        //   weapon's reach) and bursts only on hitting something (the original burst in the air 1500 ahead when it
        //   locked on nothing).
        made("ballistics", L"Баллистика ракет", L"Missile ballistics",
             L"Ракета летит на тяге до дальности оружия, потом падает; "
             L"бьёт на любом расстоянии и взрывается только от удара",
             L"A missile is powered to its weapon's reach, then falls; it hurts at any distance and bursts only on "
             L"hitting something",
             Kind::Battle, {"missileballistics"}),
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
        // HomingMissiles: a missile (a robot's or a missile turret's) takes its shooter's target as fired (none for
        //   a driven one) and keeps it while it is ahead; else it seeks the enemy most ahead within 45 degrees (the
        //   original: 10 degrees, and only nudged its course). It turns to it at up to HOMING_TURN_RATE.
        made("homing", L"Самонаведение ракет", L"Homing missiles",
             L"Ракета держит цель стрелка и круто доворачивает на неё; "
             L"без цели ищет врага впереди в широком конусе",
             L"A missile keeps to its shooter's target and turns hard to it; without one it seeks an enemy ahead in "
             L"a wide cone",
             Kind::Battle, {"homingmissiles"}),
        // DriveAimFar: over the sky or water past the map's edge the cursor's trace finds nothing and the original
        //   kept the robot aiming at the last point it found; the robot now aims along the cursor's ray.
        // TurretLaserGlow: the laser turret's muzzle glow, a robot laser's size at a point inside the thick barrel
        //   (never seen), is three times as large and before the barrel.
        // DialogEsc: Esc presses OK of the dialogs before and after the battle (the result, the statistics), as Enter
        //   does; the original ignored it there.
        // MinimapCursor: over the minimap's picture (not its buttons) the cursor is the blue cross a Move order shows
        //   instead of the arrow; the game has no cursor picture of its own for it.
        made("interface", L"\u0418\u0441\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u044f \u0438\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441\u0430", L"Interface fixes",
             L"\u0412 \u0440\u0443\u0447\u043d\u043e\u043c \u0443\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u0438 \u0440\u043e\u0431\u043e\u0442 \u0446\u0435\u043b\u0438\u0442\u0441\u044f \u0432 \u043d\u0435\u0431\u043e \u0438 \u0432 \u0432\u043e\u0434\u0443 "
             L"\u0437\u0430 \u043a\u0440\u0430\u0435\u043c \u043a\u0430\u0440\u0442\u044b; \u0441\u0432\u0435\u0442\u0438\u0442\u0441\u044f \u0441\u0442\u0432\u043e\u043b "
             L"\u043b\u0430\u0437\u0435\u0440\u043d\u043e\u0439 \u0442\u0443\u0440\u0435\u043b\u0438; "
             L"Esc \u0437\u0430\u043a\u0440\u044b\u0432\u0430\u0435\u0442 \u043e\u043a\u043d\u0430 \u0434\u043e \u0438 \u043f\u043e\u0441\u043b\u0435 \u0431\u043e\u044f; "
             L"\u043f\u0440\u0438\u0446\u0435\u043b \u043d\u0430\u0434 \u043c\u0438\u043d\u0438\u043a\u0430\u0440\u0442\u043e\u0439",
             L"A driven robot aims at the sky and at water past the map's edge; the laser turret's muzzle glows; "
             L"Esc closes the dialogs before and after the battle; a cross over the minimap",
             Kind::Personal, {"driveaimfar", "turretlaserglow", "dialogesc", "minimapcursor"}),
        // LiftSight: robots on a base's lift see the enemies in reach and are seen (the original ignored them there).
        // CaptureLift: a capturer rides the lift down when something else closed it (it stood in the air).
        // DriveReachReset: a robot its driver left loses the reach bonus too (the original kept it).
        made("pbo", L"Planetary Battles Online", L"Planetary Battles Online",
             L"\u041d\u0430 \u043b\u0438\u0444\u0442\u0435 \u0431\u0430\u0437\u044b \u0440\u043e\u0431\u043e\u0442\u044b \u0432\u0438\u0434\u044f\u0442 \u0432\u0440\u0430\u0433\u043e\u0432, "
             L"\u0437\u0430\u0445\u0432\u0430\u0442\u0447\u0438\u043a \u0435\u0434\u0435\u0442 \u0432\u043d\u0438\u0437; \u0431\u0440\u043e\u0448\u0435\u043d\u043d\u044b\u0439 \u0440\u043e\u0431\u043e\u0442 \u0442\u0435\u0440\u044f\u0435\u0442 \u0431\u043e\u043d\u0443\u0441",
             L"Robots on a base lift see enemies, a capturer rides it down; a robot left loses its bonus", Kind::Battle,
             {"liftsight", "capturelift", "drivereachreset"}),
        // RallyPoint: a person selects a base and right-clicks the ground: the robots it builds afterwards drive to
        //   that point and wait there (the original sent them to a free place near the base). A right click on the
        //   base itself clears its point. Each base has its own; a captured base loses it. A command (rally).
        made("rally", L"Точка сбора", L"Rally point",
             L"Правый щелчок по земле при выбранной базе задаёт точку, куда едут построенные ею роботы; "
             L"правый щелчок по самой базе её снимает",
             L"A right click on the ground with a base selected sets where the robots it builds go; a right click on "
             L"the base clears it",
             Kind::Battle, {"rallypoint"}),
        // RepairPlant: a repair factory of a side keeps two repairer's beams from its probes, at the rate of a robot
        //   with four repairers, on the nearest damaged robot of that side standing in its capture area; the
        //   reinforcements' lift field reaches from its hatch to the robot meanwhile. A captured factory repairs for
        //   its new side; a neutral one does nothing.
        made("repair", L"\u0420\u0435\u043c\u043e\u043d\u0442\u043d\u044b\u0435 \u0437\u0430\u0432\u043e\u0434\u044b", L"Repair factories",
             L"\u0420\u0435\u043c\u043e\u043d\u0442\u043d\u044b\u0439 \u0437\u0430\u0432\u043e\u0434 \u0447\u0438\u043d\u0438\u0442 \u0440\u043e\u0431\u043e\u0442\u043e\u0432 \u0441\u0432\u043e\u0435\u0439 \u0441\u0442\u043e\u0440\u043e\u043d\u044b "
             L"\u0432 \u0437\u043e\u043d\u0435 \u0437\u0430\u0445\u0432\u0430\u0442\u0430",
             L"A repair factory repairs its side's robots in its capture area", Kind::Battle, {"repairplant"}),
        // HelpFromAbove: the reinforcements of Universe's PBHelpFromAbove (by Klaxons): a call brings robots worth
        //   150 points (at most 180; the original 100 and 130) from its own 19 templates, stronger, with more hit
        //   points; those worth 75 and more come by the bomber. Its Репаратор head (self-repair) is not in the game:
        //   those robots have no head and repair themselves 3% a second. Only a person's side calls reinforcements.
        made("sky", L"Помощь с небес", L"Help from above",
             L"Подкрепления сильнее: роботов примерно в полтора раза больше, и они мощнее",
             L"Stronger reinforcements: about half as many robots again, and more powerful", Kind::Battle,
             {"helpfromabove"}),
        // SR2Balance: the values Universe's PBSR2Balance (by Klaxons) brings back from Space Rangers 2, over the
        //   data's (HD's): the machine gun, cannon, missile launcher and repairer fire every 70, 500, 1000 and 27 ms
        //   (55, 400, 1500, 20), a cannon's and a missile launcher's shot heat 20% and 50% (16%, 75%), the mortar
        //   hurts a turret 30 (50), the blocker head adds 30 hit points (50), the firewall head costs 40 plasma (50)
        //   and the missile launcher 10 electronics (20). Resources stay the map's. CMatrixConfig::ApplyBalance.
        made("sr2", L"Баланс КР2", L"SR2 balance",
             L"Оружие и модули как в «Космических рейнджерах 2»: скорострельность, перегрев, урон миномёта по "
             L"турелям, броня Блокиратора, цены; ресурсы не меняются",
             L"Weapons and modules as in Space Rangers 2: rates of fire, heat, the mortar's damage to turrets, the "
             L"blocker's armour, prices; resources unchanged",
             Kind::Battle, {"sr2balance"}),
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
