// MatrixGame - licensed under GPLv2 or any later version.
// Исправления интерфейса (Interface fixes), a personal mod: interface.dll in mods\interface. SelectAll (F2 by default,
// mod.txt's Key.SelectAll): all the person's robots on the map selected; and the hooks of its switches (ModHooks.hpp):
// DriveAimFar, TurretLaserGlow, DialogEsc, MinimapCursor, BuildingGroups, ConstructorKeys, ZoomFar, CameraShift,
// ChassisMotion. The tests build this file into themselves (PBO_MOD_STATIC).
#include "ModApi.hpp"
#include "ModHooks.hpp"

#include "MatrixMap.hpp"
#include "MatrixMapStatic.hpp"
#include "MatrixMultiSelection.hpp"
#include "MatrixSide.hpp"
#include "Interface/CInterface.h"
#include "Interface/CHistory.h"
#include "Interface/CCounter.h"
#include "MatrixCamera.hpp"
#include "MatrixConfig.hpp"
#include "MatrixObjectRobot.hpp"
#include "MatrixLogic.hpp"
#include "MatrixSoundManager.hpp"
#include "KeyActions.hpp"
#include "MatrixGame.h"

#include <input.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace InterfaceMod {
namespace {

std::optional<ModApi::Context> g_mod;

// As a digit selects its group: the multi-selection, a turret being placed and an order's mode end first; then
// every live robot of the person's side in one group (one robot: selected as itself).
bool select_all(int vk) {
    if (!g_MatrixMap || vk != g_mod->key("SelectAll")) return false;
    CMatrixSideUnit *ps = g_MatrixMap->GetPlayerSide();
    if (ps->IsArcadeMode() || ps->m_ConstructPanel->IsActive()) return false;
    if (CMultiSelection::m_GameSelection) CMultiSelection::m_GameSelection->End(false);
    if (ps->m_CurrentAction == BUILDING_TURRET) {
        ps->m_CannonForBuild.Delete();
        ps->m_CurrentAction = NOTHING_SPECIAL;
    }
    g_IFaceList->ResetOrderingMode();
    bool unselected = false;
    for (CMatrixMapStatic *o = CMatrixMapStatic::GetFirstLogic(); o; o = o->GetNextLogic()) {
        if (o->GetSide() != PLAYER_SIDE || !o->IsLiveRobot()) continue;
        if (!unselected) {
            unselected = true;
            ps->SelectedGroupUnselect();
            ps->GetCurSelGroup()->RemoveAll();
        }
        ps->GetCurSelGroup()->AddObject(o, -4);
    }
    const int robots = ps->GetCurSelGroup()->GetRobotsCnt();
    if (robots) {
        ps->CreateGroupFromCurrent();
        ps->Select(robots > 1 ? GROUP : ROBOT, NULL);
    }
    return true;
}

// DriveAimFar: over the sky or over water past the map's edge the cursor's trace finds nothing and keeps its last
// point, so the robot went on aiming there; it aims along the cursor's ray instead.
bool drive_aim(D3DXVECTOR3 &aim) {
    if (g_MatrixMap->m_TraceStopObj != TRACE_STOP_NONE)
        return false;
    aim = CMatrixSideUnit::DriveFarAim(g_MatrixMap->m_Camera.GetFrustumCenter(), g_MatrixMap->m_MouseDir);
    return true;
}

// TurretLaserGlow: the laser turret's muzzle point is inside its thick barrel, which hid the robot's small glow there:
// three times as large, before the barrel.
constexpr float laser_glow_size = 15;   // a robot's is 5
constexpr float laser_glow_ahead = 3;   // before the muzzle point, out of the barrel

void laser_glow(const D3DXVECTOR3 &from, const D3DXVECTOR3 &dir, D3DXVECTOR3 &at, float &size) {
    at = from + dir * laser_glow_ahead;
    size = laser_glow_size;
}

// DialogEsc: the dialogs before and after the battle close as Enter does (the original ignored Esc).
void dialog_esc() {
    if (g_MatrixMap->m_DialogModeName)
        g_IFaceList->PressHintButton(HINT_OK);
}

// MinimapCursor: a place on the map is pointed at there, as by a Move order.
bool minimap_cursor() {
    if (!CIFaceList::CursorOnMinimap())
        return false;
    g_MatrixMap->m_Cursor.Select(CURSOR_CROSS_BLUE);
    return true;
}

// BuildingGroups: Ctrl+digit keeps the selected building or turret (this PC's only, as robots' numbers), the digit
// selects it again while it stands and is the side's.
void assign_building_group(CMatrixSideUnit &side, int vk) {
    const bool building = side.m_CurrSel == BUILDING_SELECTED || side.m_CurrSel == BASE_SELECTED ||
                          side.m_CurrSel == TURRET_SELECTED;
    side.m_BuildingGroups[vk - '0'] = building && side.m_ActiveObject ? side.m_ActiveObject->GetSimId() : 0;
}

CMatrixMapStatic *building_in_group(CMatrixSideUnit &side, int vk) {
    const int id = side.m_BuildingGroups[vk - '0'];
    if (!id)
        return nullptr;
    for (CMatrixMapStatic *o = CMatrixMapStatic::GetFirstLogic(); o; o = o->GetNextLogic())
        if (o->GetSimId() == id)
            return o->GetSide() == side.m_Id && (o->IsLiveBuilding() || o->IsLiveCannon()) ? o : nullptr;
    return nullptr;
}

// ConstructorKeys: in a base's constructor the arrows are its buttons: up and down how many, left and right the
// designs before, Enter builds.
bool constructor_key(CMatrixSideUnit &side, int vk) {
    if (vk != VK_UP && vk != VK_DOWN && vk != VK_LEFT && vk != VK_RIGHT && vk != VK_RETURN)
        return false;
    if (vk == VK_UP) g_IFaceList->m_RCountControl->KeyUp();
    else if (vk == VK_DOWN) g_IFaceList->m_RCountControl->KeyDown();
    else if (vk == VK_LEFT) g_ConfigHistory->PrevConfig(nullptr);
    else if (vk == VK_RIGHT) g_ConfigHistory->NextConfig(nullptr);
    else if (g_IFaceList->m_RCountControl->GetCounter() > 0) {
        CSound::Play(S_BUILD_CLICK, SL_INTERFACE);
        side.m_Constructor->RemoteBuild(nullptr);
    }
    return true;
}

// ZoomFar: the wheel takes the camera out to twice the original's farthest distance (at 4):
// min + p * (max - min) = 2 * (min + 4 * (max - min)).
float max_zoom(int mode) {
    const SCamParam &cp = g_Config.m_CamParams[mode];
    const float span = cp.m_CamDistMax - cp.m_CamDistMin;
    return span > 0.001f ? 8.0f + cp.m_CamDistMin / span : 8.0f;
}

// CameraShift: twice as fast while Shift is held (by the keys or the screen's edge).
float camera_speed() { return Input::isKeyPressed(KA_SHIFT) ? 2.0f : 1.0f; }

// ChassisMotion: wheels and tracks drawn turning as each side of the chassis goes (back when backing, one side
// forward and the other back when turning in place); only the drawing, the battle's frame stays.
bool chassis_sides(CMatrixRobot &robot) {
    auto &s = robot.m_ChassisSides;
    const int kind = robot.m_Unit[0].u1.s1.m_Kind;
    if ((kind != RUK_CHASSIS_WHEEL && kind != RUK_CHASSIS_TRACK) ||
        !robot.m_Unit[0].m_Graph || robot.m_CurrState == ROBOT_DIP) {
        s.time = -1;
        return false;
    }
    const int now = g_MatrixMap->GetTime();
    D3DXVECTOR2 forward(robot.m_Forward.x, robot.m_Forward.y);
    D3DXVec2Normalize(&forward, &forward);
    if (s.time >= 0 && now > s.time && now - s.time <= 1000) {
        // Each side's way along the heading: the robot's own, and the turn's (the side's offset across times the
        // angle; a side's half of the chassis about half its radius out). Sideways (a strafe) turns no wheel.
        const D3DXVECTOR2 way(robot.m_PosX - s.x, robot.m_PosY - s.y);
        const float ahead = D3DXVec2Dot(&way, &s.forward);
        const float turn = std::atan2(s.forward.x * forward.y - s.forward.y * forward.x, D3DXVec2Dot(&s.forward, &forward));
        D3DXVECTOR2 across(robot.m_Unit[0].m_Matrix._11, robot.m_Unit[0].m_Matrix._12);
        D3DXVec2Normalize(&across, &across);
        // A point at +X goes ahead by turn * offset * ((z x across) . forward) as the robot turns.
        const float spin = turn * 0.5f * robot.GetRadius() * (forward.y * across.x - forward.x * across.y);
        // As the original's moving animation: a frame each k * its time, k = ANIMSPEED / the speed per logic takt.
        CVectorObjectAnim *graph = robot.m_Unit[0].m_Graph;
        const int anim = graph->GetAnimIndex();
        const int time = std::max(1, graph->VO()->GetAnimFrameTime(anim, 0));
        const float speed = kind == RUK_CHASSIS_TRACK ? ANIMSPEED_CHAISIS_TRACK : ANIMSPEED_CHAISIS_WHEEL;
        const float frames = float(graph->VO()->GetAnimFramesCount(anim));
        if (frames < 1) {  // (a model without frames of this animation: drawn as the battle has it)
            s.time = -1;
            return false;
        }
        for (int i = 0; i < 2; ++i) {
            s.step[i] = (ahead + (i ? -spin : spin)) * LOGIC_TAKT_PERIOD / (speed * time);
            s.frames[i] = std::fmod(s.frames[i] + s.step[i], frames);
        }
    }
    else {
        s.step[0] = s.step[1] = 0;
        if (s.time < 0)
            s.frames[0] = s.frames[1] = float(robot.m_Unit[0].m_Graph->GetFrame());  // from the battle's frame
    }
    s.x = robot.m_PosX;
    s.y = robot.m_PosY;
    s.forward = forward;
    s.time = now;
    return true;
}

}  // namespace

void init(ModApi::Context &mod) {
    g_mod.emplace(mod);
    mod.on_key(select_all);
    auto &hooks = ModHooks::hooks();
    hooks.drive_aim = drive_aim;
    hooks.laser_glow = laser_glow;
    hooks.dialog_esc = dialog_esc;
    hooks.minimap_cursor = minimap_cursor;
    hooks.assign_building_group = assign_building_group;
    hooks.building_in_group = building_in_group;
    hooks.constructor_key = constructor_key;
    hooks.max_zoom = max_zoom;
    hooks.camera_speed = camera_speed;
    hooks.chassis_sides = chassis_sides;
}

}  // namespace InterfaceMod

PBO_MOD_ENTRY("interface", InterfaceMod::init)
