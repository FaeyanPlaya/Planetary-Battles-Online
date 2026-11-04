// MatrixGame - licensed under GPLv2 or any later version.
// Управление турелями (Turret control), a battle mod: turrets.dll in mods	urrets. A person selects their side's
// turret with a click (or a frame). An order to attack anything live (any side's) makes the turret fire at it while it
// stays in reach, then it chooses its targets again; one at a point of the ground in reach lasts until the next order.
// A built turret is entered as a robot (the engine drives it: the lobby's manual control and its bonus as a robot's);
// dismantling a built turret frees its place and returns half its price, times the share of its hit points left
// (TurretControl). The tests build this file into themselves (PBO_MOD_STATIC).
#include "ModApi.hpp"
#include "ModHooks.hpp"

#include "MatrixMap.hpp"
#include "MatrixSide.hpp"
#include "MatrixObjectCannon.hpp"
#include "Logic/MatrixAIGroup.h"
#include "MatrixConfig.hpp"
#include "Commands.hpp"
#include "BattleRules.hpp"

#include <algorithm>

namespace TurretsMod {
namespace {

bool turrets_selectable() { return true; }

// A click on a built turret of the side, with nothing else selected, selects it.
bool turret_clicked(CMatrixSideUnit &side, CMatrixMapStatic *turret) {
    return !side.GetCurSelGroup()->GetObjectsCnt() && IS_TRACE_STOP_OBJECT(turret) && turret->IsCannon() &&
           turret->GetSide() == side.m_Id && turret->AsCannon()->IsBuilt();
}

// A built turret, as a robot standing on the ground (manual control allowed by the lobby); another person's side
// never has it.
bool turret_enter(CMatrixSideUnit &side, CMatrixCannon &cannon) {
    return BattleRules::manual_control() && cannon.IsBuilt() && side.GetArcadedObject() != &cannon;
}

// TurretAttack: the side's built turrets fire at the target while it stays in reach (or at the point); TurretDismantle: they go, half
// their price back, times the share of their hit points left (whole units, rounded down).
void turret_command(CMatrixSideUnit &side, const Commands::Command &command, CMatrixMapStatic *target) {
    for (int id : command.units) {
        CMatrixMapStatic *o = Commands::find(id);
        if (!o || !o->IsCannon() || o->GetSide() != side.m_Id || !o->AsCannon()->IsBuilt())
            continue;
        CMatrixCannon *cannon = o->AsCannon();
        if (command.type == Commands::Type::TurretAttack) {
            // At the ground point (its height there, the water's over the sea), else at the target.
            if (!command.params.empty() && command.params[0] == Commands::TURRET_AT_POINT)
                cannon->OrderFireAt(D3DXVECTOR3(command.x, command.y,
                                                std::max(g_MatrixMap->GetZ(command.x, command.y), float(WATER_LEVEL))));
            else
                cannon->OrderAttack(target);
            continue;
        }
        // Half the price back, times the share of hit points left; whole units, rounded down.
        const SCannonProps &props = g_Config.m_CannonsProps[cannon->m_Num - 1];
        const float share = std::clamp(cannon->GetHitPoint() / cannon->GetMaxHitPoint(), 0.0f, 1.0f);
        for (int r = 0; r < MAX_RESOURCES; r++)
            side.AddResourceAmount(ERes(r), int(float(props.m_Resources[r]) * 0.5f * share));
        cannon->Dismantle();
    }
}

}  // namespace

void init(ModApi::Context &) {
    auto &hooks = ModHooks::hooks();
    hooks.turrets_selectable = turrets_selectable;
    hooks.turret_clicked = turret_clicked;
    hooks.turret_enter = turret_enter;
    hooks.turret_command = turret_command;
}

}  // namespace TurretsMod

PBO_MOD_ENTRY("turrets", TurretsMod::init)
