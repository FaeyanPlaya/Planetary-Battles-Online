// MatrixGame - licensed under GPLv2 or any later version.
// Точка сбора (Rally point), a battle mod: rally.dll in mods\rally. A person selects a base and right-clicks the
// ground: the robots it builds afterwards drive to that point and wait there (the original sent them to a free place
// near the base). A right click on the base itself clears its point. Each base has its own; a captured base loses it.
// A Rally command (RallyPoint). The tests build this file into themselves (PBO_MOD_STATIC).
#include "ModApi.hpp"
#include "ModHooks.hpp"

#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "MatrixSide.hpp"
#include "MatrixObjectBuilding.hpp"
#include "Commands.hpp"
#include "BattleNotice.hpp"
#include "random.hpp"
#include "Effects/MatrixEffect.hpp"

namespace RallyMod {
namespace {

// The selected base's point is marked while it is selected (what this person sees only).
void rally_marker(CMatrixSideUnit &side, int ms) {
    if (&side == g_MatrixMap->GetPlayerSide() && side.m_CurrSel == BASE_SELECTED && side.m_ActiveObject &&
        side.m_ActiveObject->IsBuilding() && side.m_ActiveObject->AsBuilding()->m_HasRally &&
        side.m_ActiveObject->GetSide() == side.m_Id) {
        side.m_RallyMarkerTime -= ms;
        if (side.m_RallyMarkerTime <= 0) {
            side.m_RallyMarkerTime = 400;
            random::CosmeticScope cosmetic;
            const D3DXVECTOR2 &at = side.m_ActiveObject->AsBuilding()->m_Rally;
            CMatrixEffect::CreateMoveto(D3DXVECTOR3(at.x, at.y, g_MatrixMap->GetZ(at.x, at.y)));
        }
    }
    else {
        side.m_RallyMarkerTime = 0;
    }
}

bool issue_rally(CMatrixSideUnit &side, CMatrixMapStatic *clicked, const D3DXVECTOR3 &at) {
    if (side.m_CurrSel != BASE_SELECTED || !side.m_ActiveObject || !side.m_ActiveObject->IsBuilding() ||
        !side.m_ActiveObject->IsBase() || side.m_ActiveObject->GetSide() != side.m_Id)
        return false;
    const bool clear = clicked == side.m_ActiveObject;
    if (!clear && clicked != TRACE_STOP_LANDSCAPE)
        return false;
    Commands::Command command;
    command.side = side.m_Id;
    command.type = Commands::Type::Rally;
    command.target = side.m_ActiveObject->GetSimId();
    command.x = at.x;
    command.y = at.y;
    command.params = {clear ? 0 : 1};
    Commands::issue(command);
    BattleNotice::show(clear ? L"\u0422\u043e\u0447\u043a\u0430 \u0441\u0431\u043e\u0440\u0430 \u0441\u043d\u044f\u0442\u0430"
                             : L"\u0422\u043e\u0447\u043a\u0430 \u0441\u0431\u043e\u0440\u0430 \u0437\u0430\u0434\u0430\u043d\u0430");
    if (!clear) {
        // This client's interface only: the effect's randoms come from the cosmetic stream.
        random::CosmeticScope cosmetic;
        CMatrixEffect::CreateMoveto(at);
    }
    return true;
}

// A Rally command: its base's point set (params 1, on the map's land) or cleared (0).
void rally_command(CMatrixSideUnit &side, const Commands::Command &command, CMatrixMapStatic *target) {
    if (!target || !target->IsLiveBuilding() || target->GetSide() != side.m_Id ||
        !target->AsBuilding()->IsBase() || command.params.size() != 1)
        return;
    CMatrixBuilding *base = target->AsBuilding();
    if (command.params[0] == 0) {
        base->m_HasRally = false;
        return;
    }
    // On the map's land: an order to the water or past the edge would send the robots nowhere.
    const int x = TruncFloat(command.x * INVERT(GLOBAL_SCALE)), y = TruncFloat(command.y * INVERT(GLOBAL_SCALE));
    SMatrixMapUnit *unit = command.x < 0 || command.y < 0 ? NULL : g_MatrixMap->UnitGetTest(x, y);
    if (!unit || !unit->IsLand())
        return;
    base->m_HasRally = true;
    base->m_Rally = D3DXVECTOR2(command.x, command.y);
    base->m_RallySide = side.m_Id;
    return;
}

// A robot out of its base: to the base's point, and stay (no free place near the base is taken).
bool rally_spawn(CMatrixRobotAI &robot, CMatrixBuilding &base) {
    if (!base.m_HasRally || base.m_RallySide != base.m_Side)
        return false;
    robot.m_HasRally = true;
    robot.m_RallyPoint = base.m_Rally;
    RESETFLAG(g_MatrixMap->m_Flags, MMFLAG_SOUND_ORDER_ATTACK_DISABLE);
    if (robot.m_Side != 0)
        g_MatrixMap->GetSideById(robot.m_Side)->IncStatValue(STAT_ROBOT_BUILD);
    robot.m_CurrState = ROBOT_IN_SPAWN;
    base.Open();
    return true;
}

}  // namespace

void init(ModApi::Context &) {
    auto &hooks = ModHooks::hooks();
    hooks.rally_marker = rally_marker;
    hooks.issue_rally = issue_rally;
    hooks.rally_command = rally_command;
    hooks.rally_spawn = rally_spawn;
}

}  // namespace RallyMod

PBO_MOD_ENTRY("rally", RallyMod::init)
