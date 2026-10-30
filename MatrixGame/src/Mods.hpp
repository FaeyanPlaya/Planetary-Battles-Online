// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Mods of Planetary Battles Online: the project's own, built into the EXE, and folders in mods beside the EXE, each
// with a mod.txt (not read by the game yet: the folder is left empty). The engine plays the original game; a mod
// turns on switches the engine's code reads (and later brings data). A battle mod changes
// how a battle plays: the host chooses them in the lobby, every person must have the same files, and a battle's
// record names them. A personal mod changes only what this person sees or how they control.
//
// mod.txt, UTF-8 (or UTF-16 with its BOM), a Key=Value per line, # or // comments:
//   Name=..., NameEn=..., Description=..., DescriptionEn=...  shown in the lobby (Russian, English)
//   Kind=battle|personal  Default=on|off (on for a person who never chose)  Switches=Name,Name,...
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
    bool help_from_above = false;  // HelpFromAbove: stronger reinforcements (ReinforcementTemplates, 150 points)
    // Battle: Точка сбора
    bool rally_point = false;  // RallyPoint: a base sends the robots it builds to its rally point (a right click)
    // Battle: Баланс КР2
    bool sr2_balance = false;  // SR2Balance: Space Rangers 2's values of some weapons and modules (ApplyBalance)
    // Personal (each person's own; never what the battle computes): Исправления интерфейса
    bool drive_aim_far = false;  // DriveAimFar: a driven robot aims at the sky and at water past the map's edge
    bool turret_laser_glow = false;  // TurretLaserGlow: the laser turret's muzzle glow shows before its barrel
    bool dialog_esc = false;         // DialogEsc: Esc closes the dialogs before and after the battle as Enter does
    bool minimap_cursor = false;     // MinimapCursor: the pointing cross over the minimap instead of the arrow
    // Personal: Дальняя прорисовка
    bool far_view = false;  // FarView: the view, effects and shadows reach farther (far_view_scale)
    bool operator==(const Switches &) const = default;
};
const Switches &switches();
void set_switches(const Switches &switches);  // tests; apply sets them for a battle
// This person's own mods, also during a battle (the battle's switches stay), and how far the far view reaches in
// times the original (1.5 to 3).
void set_personal(const std::vector<std::string> &personal, float far_view_scale);
// How far the view reaches in times the original: 1 without the far view.
float view_scale();

enum class Kind { Battle, Personal };

struct Mod {
    std::string id;  // the folder's name in lower case: letters a-z, digits, _ and -
    std::wstring name, name_en, description, description_en;
    Kind kind = Kind::Battle;
    bool default_on = false;
    std::vector<std::string> switches;  // as written; unknown names are left out by apply
    std::string hash;                   // of every file in the folder (names and bytes); a built-in one's of its switches
    std::filesystem::path folder;       // empty: built into the EXE
    bool operator==(const Mod &) const = default;
};

// A mod folder's id from its name; empty when the name is not one.
std::string id_of(std::wstring_view folder_name);
// mod.txt's text (already decoded); fills everything but id, hash and folder.
Mod parse(std::wstring_view text);
// mod.txt's bytes: UTF-16 LE with its BOM, else UTF-8 (a BOM is skipped).
std::wstring decode(std::string_view bytes);
// The mods in this folder, by id; folders without mod.txt or with a bad name are left out.
std::vector<Mod> scan(const std::filesystem::path &folder);

inline constexpr const wchar_t *folder = L"mods";
// The mods built into the EXE, by id.
const std::vector<Mod> &builtin();
// The built-in mods and those of a folder whose ids are not built in, by id.
std::vector<Mod> catalogue(const std::vector<Mod> &scanned);
// The game's mods: the built-in ones (the mods folder is not read yet).
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

}  // namespace Mods
