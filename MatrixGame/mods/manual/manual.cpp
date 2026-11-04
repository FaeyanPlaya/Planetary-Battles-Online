// MatrixGame - licensed under GPLv2 or any later version.
// Ручное управление+ (Manual control+), a battle mod: manual.dll in mods\manual. As Universe's PBEngine: in a driven
// robot Q and E go sideways (hovercraft and antigravity at their PBEngine share of the speed; a chassis that turns to
// go sideways keeps its heading meanwhile), the bomb goes off on F instead of E, X turns the view back and forth, and
// 1-5 turn the weapon in that constructor slot off and on (it holds its fire) (ManualExtras). The keys are the
// settings' (KeyBindings). The tests build this file into themselves (PBO_MOD_STATIC).
#include "ModApi.hpp"
#include "ModHooks.hpp"

#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "MatrixSide.hpp"
#include "MatrixConfig.hpp"
#include "MatrixCamera.hpp"
#include "Commands.hpp"
#include "KeyActions.hpp"
#include "Interface/CInterface.h"

#include <input.hpp>

namespace ManualMod {
namespace {

constexpr float STRAFE_FACING_COS = 0.98480775f;  // cos 10 deg: a chassis turning to go sideways goes once this near

int drive_strafe(int keys) {
    return int((keys & Commands::DRIVE_STRAFE_RIGHT) != 0) - int((keys & Commands::DRIVE_STRAFE_LEFT) != 0);
}

bool strafes_facing(const CMatrixRobotAI &robot) {
    return robot.m_Unit[0].u1.s1.m_Kind != RUK_CHASSIS_HOVERCRAFT && robot.m_Unit[0].u1.s1.m_Kind != RUK_CHASSIS_ANTIGRAVITY;
}

float strafe_speed_share(const CMatrixRobotAI &robot) {
    switch (robot.m_Unit[0].u1.s1.m_Kind) {
        case RUK_CHASSIS_HOVERCRAFT:
            return 0.50f / 0.65f;
        case RUK_CHASSIS_ANTIGRAVITY:
            return 0.55f / 0.70f;
        default:
            return 1.0f;
    }
}

// A chassis that turns to go sideways leaves its heading on a strafe; A and D turn the heading then, and with no key
// to go it turns back to it.
bool drive_turn(CMatrixRobotAI &robot, int keys, int strafe) {
    if (!strafes_facing(robot))
        robot.m_DriveSplit = false;
    else if (strafe && !robot.m_DriveSplit) {
        robot.m_DriveHeading = D3DXVECTOR3(robot.m_Forward.x, robot.m_Forward.y, 0);
        D3DXVec3Normalize(&robot.m_DriveHeading, &robot.m_DriveHeading);
        robot.m_DriveSplit = true;
    }
    if (!robot.m_DriveSplit)
        return false;
    const int turn = int((keys & Commands::DRIVE_RIGHT) != 0) - int((keys & Commands::DRIVE_LEFT) != 0);
    if (turn) {
        D3DXMATRIX rot_mat;
        D3DXMatrixRotationZ(&rot_mat, float(turn) * robot.m_maxRotationSpeed * robot.m_SyncMul);
        D3DXVec3TransformNormal(&robot.m_DriveHeading, &robot.m_DriveHeading, &rot_mat);
        D3DXVec3Normalize(&robot.m_DriveHeading, &robot.m_DriveHeading);
    }
    if (!strafe && !(keys & (Commands::DRIVE_FORWARD | Commands::DRIVE_BACKWARD)) &&
        robot.RotateRobot(D3DXVECTOR3(robot.m_PosX, robot.m_PosY, 0) + robot.m_DriveHeading))
        robot.m_DriveSplit = false;
    return true;
}

// The move order's takt of a robot going sideways.
void drive_sideways(CMatrixRobotAI &robot, int ms, int keys, int strafe) {
    if (robot.m_DriveSplit) {
        // ManualExtras, a chassis that turns to go sideways: the way from the heading (aslant with
        // forward or back held); it turns to face it (its back to it when it goes back), then goes,
        // or with forward or back held goes the way it faces while it turns (the owner's choice: a
        // curve, as at its own turning speed).
        const D3DXVECTOR3 &heading = robot.m_DriveHeading;
        const D3DXVECTOR3 left(heading.y, -heading.x, 0);
        const float ahead = (keys & Commands::DRIVE_FORWARD)    ? 1.0f
                            : (keys & Commands::DRIVE_BACKWARD) ? -1.0f
                                                                : 0.0f;
        D3DXVECTOR3 way = heading * ahead - left * float(strafe);
        if (D3DXVec3LengthSq(&way) < 0.01f) {
            robot.StopMoving();  // (the move order's takt then places it)
            return;
        }
        D3DXVec3Normalize(&way, &way);
        const bool back = D3DXVec3Dot(&way, &heading) < -0.01f;
        const D3DXVECTOR3 face = back ? -way : way, pos(robot.m_PosX, robot.m_PosY, 0);
        D3DXVECTOR3 forward(robot.m_Forward.x, robot.m_Forward.y, 0);
        D3DXVec3Normalize(&forward, &forward);
        if (ahead == 0 && D3DXVec3Dot(&forward, &face) < STRAFE_FACING_COS) {
            robot.LowLevelMove(ms, pos + face * robot.m_maxSpeed, true, true, false, false, nullptr, 0.0f);
        }
        else {
            if (back)
                robot.RotateRobot(pos + face);  // going back, Seek does not turn it; forward, it does
            // Far: within a step Seek goes straight to the point, not the way the chassis faces.
            robot.LowLevelMove(ms, pos + way * (robot.m_maxSpeed * 100), true, true, false, back);
            D3DXVECTOR3 now(robot.m_Forward.x, robot.m_Forward.y, 0);
            D3DXVec3Normalize(&now, &now);
            if (!strafe && D3DXVec3Dot(&now, &face) > 0.9999f)
                robot.m_DriveSplit = false;  // back on the heading
        }
    }
    else if (strafe) {
        // ManualExtras, hovercraft and antigravity: sideways (with forward or back held, aslant), not
        // turning.
        D3DXVECTOR3 forward(robot.m_Forward.x, robot.m_Forward.y, 0);
        D3DXVec3Normalize(&forward, &forward);
        const D3DXVECTOR3 left(forward.y, -forward.x, 0);  // as RotateRobotLeft turns
        const float ahead = (keys & Commands::DRIVE_FORWARD)    ? 1.0f
                            : (keys & Commands::DRIVE_BACKWARD) ? -1.0f
                                                                : 0.0f;
        D3DXVECTOR3 along = forward * ahead - left * float(strafe);
        D3DXVec3Normalize(&along, &along);
        const D3DXVECTOR3 dest = D3DXVECTOR3(robot.m_PosX, robot.m_PosY, 0) + along * robot.m_maxSpeed;
        robot.LowLevelMove(ms, dest, true, true, false, false, &along, strafe_speed_share(robot));
    }
}

// A slot its driver turned off holds its fire.
bool slot_off(int keys, int slot) {
    return slot >= 1 && slot <= MAX_WEAPON_CNT && (keys & (Commands::DRIVE_SLOT_OFF << (slot - 1)));
}

int drive_keys(CMatrixSideUnit &side) {
    int keys = 0;
    if (Input::isKeyPressed(KA_UNIT_STRAFE_LEFT))
        keys |= Commands::DRIVE_STRAFE_LEFT;
    if (Input::isKeyPressed(KA_UNIT_STRAFE_RIGHT))
        keys |= Commands::DRIVE_STRAFE_RIGHT;
    return keys | side.m_DriveSlotsOff;
}

void toggle_drive_slot(CMatrixSideUnit &side, int slot) {
    if (!side.IsRobotMode() || slot < 1 || slot > MAX_WEAPON_CNT)
        return;
    const CMatrixRobotAI *robot = side.GetArcadedObject()->AsRobot();
    for (int i = 0; i < robot->m_UnitCnt; ++i)
        if (robot->m_Unit[i].m_Type == MRT_WEAPON && robot->m_Unit[i].u1.s1.m_Slot == slot) {
            side.m_DriveSlotsOff ^= Commands::DRIVE_SLOT_OFF << (slot - 1);
            if (g_IFaceList)
                g_IFaceList->CreateWeaponDynamicStatics();
            return;
        }
}

// The bomb on F (E goes sideways; a bomb turned off stays), X looks back, 1-5 turn weapon slots off and on.
bool drive_key(CMatrixSideUnit &side, CMatrixRobotAI &robot, int vk) {
    if (Input::isKeyPressed(KA_UNIT_BOOM_EXTRAS)) {
        if (!side.DriveKindOff(&robot, RUK_WEAPON_BOMB))
            side.RequestDrive(Commands::DriveAction::Explode, &robot);
        return true;
    }
    if (Input::isKeyPressed(KA_UNIT_REAR_VIEW)) {
        g_MatrixMap->m_Camera.ToggleRearView();
        return true;
    }
    for (int slot = 1; slot <= 5; ++slot) {
        if (vk == g_Config.m_KeyActions[KA_UNIT_SLOT1 + slot - 1]) {
            // (As the panel shows the weapons: g_IFaceList knows where.)
            if (g_IFaceList)
                side.ToggleDriveSlot(g_IFaceList->SlotForKey(&robot, slot));
            return true;
        }
    }
    return false;
}

// The rear view stays while a robot is driven.
bool rear_view() { return g_MatrixMap->GetPlayerSide()->IsRobotMode(); }

}  // namespace

void init(ModApi::Context &) {
    auto &hooks = ModHooks::hooks();
    hooks.drive_strafe = drive_strafe;
    hooks.drive_turn = drive_turn;
    hooks.drive_sideways = drive_sideways;
    hooks.slot_off = slot_off;
    hooks.drive_keys = drive_keys;
    hooks.toggle_drive_slot = toggle_drive_slot;
    hooks.drive_key = drive_key;
    hooks.rear_view = rear_view;
}

}  // namespace ManualMod

PBO_MOD_ENTRY("manual", ManualMod::init)
