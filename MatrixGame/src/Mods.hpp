// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Mods of Planetary Battles Online: folders in mods beside the EXE (the project's own among them, MatrixGame/mods),
// each with a mod.txt and, for a mod with code, its DLL (ModApi.hpp). The engine plays the original game; a mod
// turns on switches the engine's code reads, or runs its own code (and later brings data). A battle mod changes
// how a battle plays: the host chooses them in the lobby, every person must have the same files, and a battle's
// record names them. A personal mod changes only what this person sees or how they control.
//
// mod.txt, UTF-8 (or UTF-16 with its BOM), a Key=Value per line, # or // comments:
//   Name=..., NameEn=..., Description=..., DescriptionEn=...  shown in the lobby (Russian, English); Author=...
//   (Description= again: the next line of the description, as SR HD's FullDescription=; an empty one: a blank line)
//   Kind=battle|personal  Default=on|off (on for a person who never chose)  Switches=Name,Name,...
//   Dll=name.dll  the mod's code, a file of its folder (none: a mod of switches or data only)
//   Dependence=id,id  mods it needs on with it; Conflict=id,id  mods it cannot be on with (as Space Rangers HD's
//   ModuleInfo.txt): switching it on asks to switch on what it needs (no: it stays off); a battle does not start
//   while mods that conflict are on
//   Key.<Id>=KEY_F2|Русское название|English name|Group  a key of the mod (the keys page; CFG\keys.txt's
//   <mod>.<Id>); Group: Camera, Driving, Orders, Building, else the page's Mods group
//   Setting.<Id>=a,b,c|b|Русское название|English name  a row of the settings' mods page (CFG\mods.txt's
//   <mod>.<Id>): its values, its default (not one of them: the first)
//   (the English name and the group may be left out; <Id>: letters, digits and _)
namespace Mods {

// What the mods switched on. Everything off: the original game.
struct Switches {
    // Battle: Planetary Battles Online
    bool lift_sight = false;         // LiftSight: robots on a base's lift see the enemies in reach, and are seen
    bool capture_lift = false;       // CaptureLift: a capturer rides the lift down when something else closed it
    bool drive_reach_reset = false;  // DriveReachReset: a robot left by its driver loses the reach bonus too
    // Battle: Исправления боя
    bool capture_no_timeout = false;  // CaptureNoTimeout: a robot capturing a base is not killed by the 10 s lift limit
    bool stop_in_place = false;       // StopInPlace: Stop halts robots where they stand
    // Battle: Ремонтные заводы
    bool repair_plant = false;  // RepairPlant: a repair factory repairs its side's robots in its capture area
    // Battle: Управление турелями
    bool turret_control = false;  // TurretControl: a side's turrets are selected, ordered, driven and dismantled
    // Battle: Автоподрыв
    bool auto_boom = false;  // AutoBoom: a person's robot sets its bomb off as it dies, unless turned off for it
    // Battle: Самонаведение ракет
    bool homing_missiles = false;  // HomingMissiles: a missile keeps to its shooter's target and turns hard to it
    // Battle: Баллистика ракет
    bool missile_ballistics = false;  // MissileBallistics: the engine burns out at the weapon's reach, then the
                                      // missile falls; it hurts at any distance and bursts only on hitting
    // Battle: Помощь с небес
    // Battle: Ручное управление+
    bool manual_extras = false;  // ManualExtras: in a robot Q/E go sideways, F sets the bomb off, X looks back, 1-5 slots
    // Battle: Точка сбора
    bool rally_point = false;  // RallyPoint: a base sends the robots it builds to its rally point (a right click)
    // Battle: Баланс КР2
    // Personal (each person's own; never what the battle computes): Исправления интерфейса
    bool drive_aim_far = false;  // DriveAimFar: a driven robot aims at the sky and at water past the map's edge
    bool turret_laser_glow = false;  // TurretLaserGlow: the laser turret's muzzle glow shows before its barrel
    bool dialog_esc = false;         // DialogEsc: Esc closes the dialogs before and after the battle as Enter does
    bool minimap_cursor = false;     // MinimapCursor: the pointing cross over the minimap instead of the arrow
    bool building_groups = false;    // BuildingGroups: Ctrl+digit keeps the selected building or turret, a digit selects it
    bool constructor_keys = false;   // ConstructorKeys: in the constructor up/down: how many, left/right: the design, Enter: build
    bool zoom_far = false;           // ZoomFar: the wheel takes the camera twice as far out
    bool camera_shift = false;       // CameraShift: the camera moves over the map twice as fast while Shift is held
    bool chassis_motion = false;     // ChassisMotion: wheels and tracks drawn as each side moves (back, turning)
    // Personal: Дальняя прорисовка
    bool far_view = false;  // FarView: the view, effects and shadows reach farther (far_view_scale)
    // Personal: Вид от первого лица
    bool first_person = false;  // FirstPerson: the wheel all the way in, in a robot or turret, looks from it
    // Personal: Небо высокого разрешения
    bool new_sky = false;  // NewSky: the game's skies at 2048 from Universe's PBNewSky, when the game has it
    bool operator==(const Switches &) const = default;
};
const Switches &switches();
void set_switches(const Switches &switches);  // tests; apply sets them for a battle
// Tests: the mods on (on), as apply and set_personal set them.
void set_on(const std::vector<std::string> &battle, const std::vector<std::string> &personal);
// This person's own mods, also during a battle (the battle's switches stay), and how far the far view reaches in
// times the original (1.5 to 3).
void set_personal(const std::vector<std::string> &personal, float far_view_scale);
// How far the view reaches in times the original: 1 without the far view.
float view_scale();

enum class Kind { Battle, Personal };

// Why a mod of the folder cannot be used (it is then as if it were not there: a battle needing it is refused).
enum class Problem {
    None,
    NoDll,        // Dll= names no file of its folder
    NotLoaded,    // Windows would not load the DLL, or it has no pbo_mod_init
    OtherEngine,  // built for another version of the game (PBO_ENGINE_ID)
    InitFailed,   // its init threw
};

struct KeyLine {
    std::string id;  // as written
    std::wstring key, russian, english, group;  // key: the game's name for it (KEY_F2) or a number
    bool operator==(const KeyLine &) const = default;
};
struct SettingLine {
    std::string id;
    std::vector<std::wstring> values;
    std::wstring default_value, russian, english;
    bool operator==(const SettingLine &) const = default;
};

struct Mod {
    std::string id;  // the folder's name in lower case: letters a-z, digits, _ and -
    std::wstring name, name_en, description, description_en;
    std::wstring author;  // Author=: who made it (the mod's window shows it)
    Kind kind = Kind::Battle;
    bool default_on = false;
    std::vector<std::string> switches;  // as written; unknown names are left out by apply
    std::string hash;                   // of every file in the folder (names and bytes)
    std::filesystem::path folder;
    std::wstring dll;                   // Dll=: its code in the folder; empty: none
    Problem problem = Problem::None;    // set by load_code
    std::vector<std::string> depends, conflicts;  // Dependence=, Conflict=
    std::vector<KeyLine> keys;                    // Key.<Id>=
    std::vector<SettingLine> settings;            // Setting.<Id>=
    bool operator==(const Mod &) const = default;
};

// A mod folder's id from its name; empty when the name is not one.
std::string id_of(std::wstring_view folder_name);
// mod.txt's text (already decoded); fills everything but id, hash and folder.
Mod parse(std::wstring_view text);
// A mod folder's fingerprint (Mod::hash): every file under it, by its path from there in lower case, and its bytes.
std::string folder_hash(const std::filesystem::path &folder);
// mod.txt's bytes: UTF-16 LE with its BOM, else UTF-8 (a BOM is skipped).
std::wstring decode(std::string_view bytes);
// The mods in this folder, by id; folders without mod.txt or with a bad name are left out.
std::vector<Mod> scan(const std::filesystem::path &folder);

inline constexpr const wchar_t *folder = L"mods";
// Loads the DLLs of these mods of a folder (once a process: they stay loaded) and calls their init; a mod that
// cannot be used gets its problem, written to test.log.
void load_code(std::vector<Mod> &mods);
// The game's mods: those of the mods folder beside the EXE, their code loaded and their keys given to KeyBindings
// (key_actions).
const std::vector<Mod> &installed();
const Mod *find(const std::vector<Mod> &mods, std::string_view id);

// The battle (personal) mods a person who never chose plays with: those installed with Default=on, in the
// catalogue's order.
std::vector<std::string> default_battle(const std::vector<Mod> &mods);
std::vector<std::string> default_personal(const std::vector<Mod> &mods);

// "pbo,turrets" and back; "-" is none. ids are not checked here.
std::string join(const std::vector<std::string> &ids);
std::vector<std::string> split(std::string_view text);

// The switches of these battle mods (installed ones; a personal mod's switches are not taken). Returns the ids
// that are not installed battle mods.
std::vector<std::string> collect(const std::vector<Mod> &mods, const std::vector<std::string> &battle, Switches &out);
// Adds the switches of these personal mods (only personal switches, from personal mods) to `out`. Returns the ids
// that are not installed personal mods.
std::vector<std::string> collect_personal(const std::vector<Mod> &mods, const std::vector<std::string> &personal,
                                          Switches &out);
// Sets the switches of a battle about to load: the battle's mods and this person's own. False (and nothing set) when
// a battle mod is missing, named in `missing`; a missing personal mod is only left out (named in `skipped`).
bool apply(const std::vector<std::string> &battle, const std::vector<std::string> &personal, std::string &missing,
           std::string &skipped);
// Whether a mod is on now: one of the battle's (apply) or of this person's (apply, set_personal).
bool on(std::string_view id);

// The ids on after switching `id` (of these mods): off, it goes with every mod that depends on it; on, it comes with
// what it depends on (those of `mods`; what conflicts stays: conflicting). Order kept, new at the end.
std::vector<std::string> toggled(const std::vector<Mod> &mods, std::vector<std::string> on, const std::string &id);
// What switching `id` on would bring (toggled): the mods of `mods` it needs, directly or through another, not on yet.
std::vector<std::string> needs(const std::vector<Mod> &mods, const std::vector<std::string> &on, const std::string &id);
// The pairs of these ids that cannot be on together (either names the other in Conflict=), in the order of `on`.
std::vector<std::pair<std::string, std::string>> conflicting(const std::vector<Mod> &mods,
                                                             const std::vector<std::string> &on);

// A personal mod this build vouches for, by its fingerprint: one of the release's own mods, whose fingerprints the EXE
// carries (its export pbo_verified_mods, "id:hash;id:hash;", written at the build by
// engine/ModHash.cpp). The lobby's host may let only these play (Lobby::State).
bool verified(std::string_view id, std::string_view hash);
// The battle lets only verified personal mods play (BattleRules::Rules::verified_personal): any other is left out by
// apply and set_personal as if it were not installed.
void set_verified_only(bool only);

// The settings of mods (Setting.<Id>=) the person chose, by <mod>.<Id>; also of mods not installed now.
using SettingValues = std::map<std::string, std::wstring>;
inline constexpr const wchar_t *settings_file = L"CFG\\mods.txt";  // this person's, beside the EXE as keys.txt
// <mod>.<Id>=value lines (UTF-8); none: an empty list.
SettingValues load_settings(const std::filesystem::path &path);
// Replaces the file. False with `error` when it cannot.
bool save_settings(const std::filesystem::path &path, const SettingValues &values, std::string &error);
// The person's now (read from settings_file the first time; the settings window sets them).
const SettingValues &setting_values();
void set_setting_values(SettingValues values);
// A setting's value: the person's when it is one of its values, else its default.
inline std::wstring setting_value(const Mod &mod, const SettingLine &setting, const SettingValues &values) {
    const auto found = values.find(mod.id + "." + setting.id);
    if (found != values.end() && std::ranges::find(setting.values, found->second) != setting.values.end())
        return found->second;
    return setting.default_value;
}
inline const SettingLine *find_setting(const Mod &mod, std::string_view id) {
    for (const auto &setting : mod.settings)
        if (setting.id == id) return &setting;
    return nullptr;
}

}  // namespace Mods
