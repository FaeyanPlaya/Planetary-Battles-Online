// MatrixGame - licensed under GPLv2 or any later version.
// Исправления боя (Battle fixes), a battle mod: fixes.dll in mods\fixes. A robot capturing a base is not killed by
// the 10 s limit of a robot with a base (CaptureNoTimeout); Stop halts robots where they stand (StopInPlace). The
// tests build this file into themselves (PBO_MOD_STATIC).
#include "ModApi.hpp"
#include "ModHooks.hpp"

#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "MatrixSide.hpp"

namespace FixesMod {
namespace {

// CaptureNoTimeout: a robot with a base (one just built, leaving its lift; or one capturing it) dies after 10 s with
// it, so that a robot stuck on a lift frees the base. A capture held up longer (its lift kept from closing) killed the
// capturer: the count stops while the robot captures.
bool base_time_held(CMatrixRobotAI &robot) { return robot.FindOrderLikeThat(ROT_CAPTURE_FACTORY); }

// StopInPlace: each robot of the group halts where it stands and keeps that spot as its place. The original spread
// the group over the free places round its centre, so robots walked on.
bool stop_in_place(CMatrixSideUnit &side, int group) {
    for (CMatrixMapStatic *obj = CMatrixMapStatic::GetFirstLogic(); obj; obj = obj->GetNextLogic()) {
        if (!obj->IsLiveRobot() || obj->GetSide() != side.m_Id || obj->AsRobot()->GetGroupLogic() != group ||
            obj->AsRobot()->IsDisableManual())
            continue;
        // As the side breaks a robot's order: one that cannot break it now keeps it.
        CMatrixRobotAI *robot = obj->AsRobot();
        if ((robot->GetEnv()->m_OrderNoBreak = !robot->CanBreakOrder()))
            continue;
        robot->StopCapture();
        robot->RemoveOrder(ROT_MOVE_RETURN);
        robot->StopMoving();
        robot->GetEnv()->m_Place = -1;
        robot->GetEnv()->m_PlaceAdd = CPoint(robot->GetMapPosX(), robot->GetMapPosY());
    }
    return true;
}

}  // namespace

void init(ModApi::Context &) {
    auto &hooks = ModHooks::hooks();
    hooks.base_time_held = base_time_held;
    hooks.stop_in_place = stop_in_place;
}

}  // namespace FixesMod

PBO_MOD_ENTRY("fixes", FixesMod::init)
