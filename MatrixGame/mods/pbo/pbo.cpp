// MatrixGame - licensed under GPLv2 or any later version.
// Planetary Battles Online, a battle mod: pbo.dll in mods\pbo. Fixes of what network battles showed: robots on a
// base's lift see the enemies in reach and are seen (LiftSight), a capturer rides the lift down when something else
// closed it (CaptureLift), a robot its driver left loses the reach bonus too (DriveReachReset), only the selected
// building's turret places are shown (TurretPlaces), a capturer on a base's lift does not drive off it to give way
// (CaptureStay). The tests build this file into themselves (PBO_MOD_STATIC).
#include "ModApi.hpp"
#include "ModHooks.hpp"

#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "MatrixObjectBuilding.hpp"

namespace PboMod {
namespace {

// LiftSight: a robot on a base's lift (leaving it, or capturing the base) stands off the move grid. The path check
// that sorts the enemies a robot sees into reachable and ignored failed there both ways: the robot ignored every
// enemy it saw for 5 s at a time, and every enemy ignored it, point-blank. Seen and in reach is enough there.
bool off_move_grid(const CMatrixRobotAI &robot, const CPoint &from, const CPoint &to) {
    const byte mm = robot.m_Unit[0].u1.s1.m_Kind - 1;  // as the path check takes it
    return g_MatrixMap->FindNearPlace(mm, from) < 0 || g_MatrixMap->FindNearPlace(mm, to) < 0;
}

// CaptureLift: the lift began to close under the capturer (the base's new robot left it or died on it): raised again,
// as on the way in (the original left the robot standing in the air where the top had been).
void capture_raise(CMatrixBuilding &base) {
    if (base.IsBase() && base.m_State != BASE_OPENED && base.m_State != BASE_OPENING)
        base.Open();
}

// CaptureLift: not riding the lift yet, it goes down with the robot only from the top (it stood on it in the air when
// something else had closed it).
bool capture_wait(CMatrixRobotAI &robot, CMatrixBuilding &base) {
    if (robot.m_CurrState == ROBOT_BASE_CAPTURE || base.m_State == BASE_OPENED)
        return false;
    if (base.m_State != BASE_OPENING)
        base.Open();
    return true;
}

// DriveReachReset: the original kept the weapons' reach bonus of a robot its driver left (unless the driver went on to
// another robot).
bool drive_reach_reset() { return true; }

// TurretPlaces: a turret lost while this person chose a place for one at any building showed its own building's
// places, and nothing took them off: green spots by another building, where no turret could be ordered.
bool turret_places() { return true; }

// CaptureStay: a capturer's way of giving way (a move and a return over its capture) waited while it drove onto the
// lift, and went on from there: the robot drove off the lift as it set up and captured (once into blocked cells that
// threw it behind the base), and the lift going down without it killed it. From the lift on it waits.
bool capture_stay(int phase) {
    return phase == ROP_CAPTURE_IN_POSITION || phase == ROP_CAPTURE_SETTING_UP || phase == ROP_CAPTURING;
}

}  // namespace

void init(ModApi::Context &) {
    auto &hooks = ModHooks::hooks();
    hooks.off_move_grid = off_move_grid;
    hooks.capture_raise = capture_raise;
    hooks.capture_wait = capture_wait;
    hooks.drive_reach_reset = drive_reach_reset;
    hooks.turret_places = turret_places;
    hooks.capture_stay = capture_stay;
}

}  // namespace PboMod

PBO_MOD_ENTRY("pbo", PboMod::init)
