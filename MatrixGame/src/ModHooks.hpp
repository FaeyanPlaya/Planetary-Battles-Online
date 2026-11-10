// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <map>
#include <string>

class CMatrixCamera;
class CMatrixRobot;
class CMatrixSideUnit;
class CMatrixMapStatic;
class CMatrixRobotAI;
class CMatrixBuilding;
class CMatrixConfig;
namespace Base {
class CPoint;
}
struct D3DXVECTOR3;
struct SMOProps;
class CMatrixCannon;
namespace Commands {
struct Command;
}

// Where the engine runs our mods' code (MatrixGame/mods/<id>/<id>.cpp, each its mod's DLL, whose init sets its hooks
// here). The engine asks a hook only while its mod's switch is on (Mods::switches, from the mod's Switches=); a hook
// nothing set leaves the original game (a test program without the mods). A mod's switches are on only when its DLL
// loaded (Mods::apply refuses a mod that cannot be used), so a mod is all of its code or none of it. Hooks are added
// as mods need them; each names the mod that sets it.
namespace ModHooks {

struct Hooks {
    // Дальняя прорисовка (farview, FarView): how many times farther the view, the fog, effects and objects reach,
    // from the person's choice (FarViewScale=); how far shadows are drawn at that scale.
    float (*view_scale)(float chosen) = nullptr;
    float (*shadow_distance)(float scale) = nullptr;

    // Вид от первого лица (firstperson, FirstPerson): the wheel turned in with the camera all the way in (true: the
    // step was the mod's: the view goes into the robot or turret); whether the view stays in it in this camera mode.
    bool (*first_person_in)(CMatrixCamera &camera) = nullptr;
    bool (*first_person_keeps)(const CMatrixCamera &camera, int mode) = nullptr;

    // Небо высокого разрешения (newsky, NewSky): the sides of a sky of the map (Fore, Back, Left, Right) to load
    // instead of the game's, by their texture paths; empty: the game's.
    std::map<std::wstring, std::wstring> (*sky_sides)(const std::wstring &sky) = nullptr;

    // Исправления интерфейса (interface):
    // DriveAimFar: where a driven robot aims when the cursor's trace found nothing (true: `aim` set).
    bool (*drive_aim)(D3DXVECTOR3 &aim) = nullptr;
    // TurretLaserGlow: a laser turret's muzzle glow, from its muzzle point and the beam's way: where and how large.
    void (*laser_glow)(const D3DXVECTOR3 &from, const D3DXVECTOR3 &dir, D3DXVECTOR3 &at, float &size) = nullptr;
    // DialogEsc: Esc over the dialogs before and after the battle.
    void (*dialog_esc)() = nullptr;
    // MinimapCursor: the cursor where no order chose it (true: the mod chose it).
    bool (*minimap_cursor)() = nullptr;
    // BuildingGroups: Ctrl+digit over a selected building or turret; the digit's building (null: none).
    void (*assign_building_group)(CMatrixSideUnit &side, int vk) = nullptr;
    CMatrixMapStatic *(*building_in_group)(CMatrixSideUnit &side, int vk) = nullptr;
    // ConstructorKeys: a key while a base's constructor is open (true: the mod took it).
    bool (*constructor_key)(CMatrixSideUnit &side, int vk) = nullptr;
    // ZoomFar: the camera's farthest distance parameter in a camera mode (the original's is 4).
    float (*max_zoom)(int mode) = nullptr;
    // CameraShift: the strategic camera's speed now, times the original's.
    float (*camera_speed)() = nullptr;
    // ChassisMotion: a robot's chassis sides' frames for this frame (CMatrixRobot::m_ChassisSides); true: drawn so.
    bool (*chassis_sides)(CMatrixRobot &robot) = nullptr;

    // Planetary Battles Online (pbo): LiftSight: whether a robot's sight between two cells skips the path check (a
    // robot on a base's lift, off the move grid); CaptureLift: the capturer's lift raised again under it as it sets
    // up, and waited for at the top (true: still waiting); DriveReachReset: a robot its driver left loses the reach
    // bonus too; TurretPlaces: only the selected building's turret places shown (true); CaptureStay: whether a capture
    // in this phase keeps the robot from the moves under it (true from the lift on).
    bool (*off_move_grid)(const CMatrixRobotAI &robot, const Base::CPoint &from, const Base::CPoint &to) = nullptr;
    void (*capture_raise)(CMatrixBuilding &base) = nullptr;
    bool (*capture_wait)(CMatrixRobotAI &robot, CMatrixBuilding &base) = nullptr;
    bool (*drive_reach_reset)() = nullptr;
    bool (*turret_places)() = nullptr;
    bool (*capture_stay)(int phase) = nullptr;

    // Исправления боя (fixes): CaptureNoTimeout: whether a robot with a base keeps it past the 10 s; StopInPlace: Stop
    // for a player's group (true: its robots were halted where they stand).
    bool (*base_time_held)(CMatrixRobotAI &robot) = nullptr;
    bool (*stop_in_place)(CMatrixSideUnit &side, int group) = nullptr;

    // Ремонтные заводы (repair, RepairPlant): a repair factory's takt while it has a side.
    void (*repair_takt)(CMatrixBuilding &factory, int ms) = nullptr;


    // Автоподрыв (autoboom, AutoBoom): whether a person's robot sets its bomb off as it dies; a right click on the bomb
    // order; that order's hint; an AutoBoom command; the selected bombers' switch turned (true: on), and whether all
    // of them have it on.
    bool (*auto_boom)(const CMatrixRobotAI &robot) = nullptr;
    void (*bomb_right_click)() = nullptr;
    std::wstring (*bomb_hint)(bool russian) = nullptr;
    void (*auto_boom_command)(CMatrixSideUnit &side, const Commands::Command &command) = nullptr;
    bool (*toggle_auto_boom)(CMatrixSideUnit &side) = nullptr;
    bool (*selected_auto_boom)(CMatrixSideUnit &side) = nullptr;

    // Точка сбора (rally, RallyPoint): the selected base's point marked (what this person sees); a right click with a
    // base selected (true: a rally order); a Rally command; a robot out of its base (true: sent to the base's point).
    void (*rally_marker)(CMatrixSideUnit &side, int ms) = nullptr;
    bool (*issue_rally)(CMatrixSideUnit &side, CMatrixMapStatic *clicked, const D3DXVECTOR3 &at) = nullptr;
    void (*rally_command)(CMatrixSideUnit &side, const Commands::Command &command, CMatrixMapStatic *target) = nullptr;
    bool (*rally_spawn)(CMatrixRobotAI &robot, CMatrixBuilding &base) = nullptr;

    // Баллистика ракет (ballistics, MissileBallistics): a missile's engine burnt out (SMOProps' hm.burnt) as it flies;
    // its fall burnt out, this takt; whether its flight is over (true: it goes); whether a missile hurts past its
    // weapon's reach.
    void (*missile_burn)(SMOProps &props) = nullptr;
    void (*missile_fall)(SMOProps &props, float takt) = nullptr;
    bool (*missile_ends)(const SMOProps &props) = nullptr;
    bool (*missile_hurts_far)() = nullptr;

    // Самонаведение ракет (homing, HomingMissiles): a missile's target kept while it lives and is ahead (`dir`: its way
    // now); the sphere ahead, the cone and the kinds of objects (TRACE_ bits) it seeks a target in; whether it waits
    // now instead of seeking (it has a target, or it has just been launched); its course this takt.
    void (*homing_keep)(SMOProps &props, const D3DXVECTOR3 &dir) = nullptr;
    void (*homing_seek)(const SMOProps &props, float &radius, float &min_cos, int &mask) = nullptr;
    bool (*homing_waits)(const SMOProps &props) = nullptr;
    void (*homing_turn)(SMOProps &props, const D3DXVECTOR3 &dir, float takt) = nullptr;

    // Ручное управление+ (manual, ManualExtras): the sideways of a driver's keys (1 right, -1 left, 0 none); a driven
    // robot's turning this takt (true: the mod's, a chassis turning to go sideways); its going sideways (its move
    // order's takt); whether a weapon slot is held off by the driver's keys; the keys this person adds to a robot
    // driven; a weapon slot turned off or on; a key in a driven robot (true: the mod's); whether the rear view stays.
    int (*drive_strafe)(int keys) = nullptr;
    bool (*drive_turn)(CMatrixRobotAI &robot, int keys, int strafe) = nullptr;
    void (*drive_sideways)(CMatrixRobotAI &robot, int ms, int keys, int strafe) = nullptr;
    bool (*slot_off)(int keys, int slot) = nullptr;
    int (*drive_keys)(CMatrixSideUnit &side) = nullptr;
    void (*toggle_drive_slot)(CMatrixSideUnit &side, int slot) = nullptr;
    bool (*drive_key)(CMatrixSideUnit &side, CMatrixRobotAI &robot, int vk) = nullptr;
    bool (*rear_view)() = nullptr;

    // Управление турелями (turrets, TurretControl): whether a frame selects turrets; whether a click on this object
    // selects it (a built turret of the side, nothing else selected); whether the side may enter this turret now;
    // a TurretAttack or TurretDismantle command.
    bool (*turrets_selectable)() = nullptr;
    bool (*turret_clicked)(CMatrixSideUnit &side, CMatrixMapStatic *clicked) = nullptr;
    bool (*turret_enter)(CMatrixSideUnit &side, CMatrixCannon &cannon) = nullptr;
    void (*turret_command)(CMatrixSideUnit &side, const Commands::Command &command, CMatrixMapStatic *target) = nullptr;
};

Hooks &hooks();

}  // namespace ModHooks
