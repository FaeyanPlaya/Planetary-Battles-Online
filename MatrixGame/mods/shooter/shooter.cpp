// MatrixGame - licensed under GPLv2 or any later version.
// Шутер (Shooter), a battle mod: shooter.dll in mods\shooter, with Manual control+ (its sideways). The battle's side:
// a driven robot's hull turns to the aim by itself (the game's own, at its speed); its chassis turns only to where it
// goes, the moves going by the aim: Manual control+'s sideways with the aim's way as the heading (forward to it, back
// from it, sideways across it); hovercraft and antigravity, whose way is not their facing, turn with the hull. This
// person's side: the turn keys go sideways (the strafe keys are free), the mouse turns the view by its setting. The
// engine's side (the view, the cursor, the ring): Shooter.cpp. The tests build this file into themselves
// (PBO_MOD_STATIC).
#include "ModApi.hpp"
#include "ModHooks.hpp"

#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "MatrixSide.hpp"
#include "Commands.hpp"
#include "KeyActions.hpp"

#include <input.hpp>

#include <cwchar>
#include <optional>

namespace ShooterMod {
namespace {

constexpr float near_aim = 10.0f;     // an aim nearer the robot than this gives no way: the hull's is taken
constexpr float look_per_pixel = 0.0025f;  // radians of the view a pixel of the mouse, at sensitivity 1

std::optional<ModApi::Context> g_mod;

bool hovers(const CMatrixRobotAI &robot) {
    return robot.m_Unit[0].u1.s1.m_Kind == RUK_CHASSIS_HOVERCRAFT || robot.m_Unit[0].u1.s1.m_Kind == RUK_CHASSIS_ANTIGRAVITY;
}

// The way on the ground from the robot to the aim (the hull's when the aim is about under it).
D3DXVECTOR3 aim_way(const CMatrixRobotAI &robot, const D3DXVECTOR3 &aim) {
    D3DXVECTOR3 way(aim.x - robot.m_PosX, aim.y - robot.m_PosY, 0);
    if (D3DXVec3LengthSq(&way) < near_aim * near_aim) way = D3DXVECTOR3(robot.m_HullForward.x, robot.m_HullForward.y, 0);
    if (D3DXVec3LengthSq(&way) < 1e-6f) way = D3DXVECTOR3(robot.m_Forward.x, robot.m_Forward.y, 0);
    D3DXVec3Normalize(&way, &way);
    return way;
}

void turn(CMatrixRobotAI &robot, int, const D3DXVECTOR3 &aim) {
    if (hovers(robot)) {
        // With the hull, at the chassis' own speed; its moves are Manual control+'s (forward its facing, sideways
        // across it), so they follow the aim as the hull does.
        robot.m_DriveSplit = false;
        D3DXVECTOR3 hull(robot.m_HullForward.x, robot.m_HullForward.y, 0);
        if (D3DXVec3LengthSq(&hull) > 1e-6f) {
            D3DXVec3Normalize(&hull, &hull);
            robot.RotateRobot(D3DXVECTOR3(robot.m_PosX, robot.m_PosY, 0) + hull);
        }
        return;
    }
    // Wheels and tracks: the heading is the aim's way; Manual control+'s sideways turns the chassis to where it goes
    // (and with no key held it stands as it is: no turning back to the heading).
    robot.m_DriveHeading = aim_way(robot, aim);
    robot.m_DriveSplit = true;
}

int keys(CMatrixSideUnit &) {
    int keys = 0;
    if (Input::isKeyPressed(KA_UNIT_LEFT) || Input::isKeyPressed(KA_UNIT_LEFT_ALT))
        keys |= Commands::DRIVE_STRAFE_LEFT;
    if (Input::isKeyPressed(KA_UNIT_RIGHT) || Input::isKeyPressed(KA_UNIT_RIGHT_ALT))
        keys |= Commands::DRIVE_STRAFE_RIGHT;
    return keys;
}

float look() {
    float sensitivity = 1.0f;
    if (g_mod) {
        const std::wstring value = g_mod->setting("Sensitivity");
        wchar_t *end = nullptr;
        const float read = std::wcstof(value.c_str(), &end);
        if (!value.empty() && end == value.c_str() + value.size() && read > 0 && read <= 10) sensitivity = read;
    }
    return look_per_pixel * sensitivity;
}

}  // namespace

void init(ModApi::Context &mod) {
    g_mod = mod;
    auto &hooks = ModHooks::hooks();
    hooks.shooter_turn = turn;
    hooks.shooter_keys = keys;
    hooks.shooter_look = look;
}

}  // namespace ShooterMod

PBO_MOD_ENTRY("shooter", ShooterMod::init)
