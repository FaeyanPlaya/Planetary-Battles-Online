// MatrixGame - licensed under GPLv2 or any later version.
// Ремонтные заводы (Repair factories), a battle mod: repair.dll in mods\repair. The original game's repair factory
// did nothing; with the mod it repairs its side's robots in its capture area (RepairPlant). The tests build this file
// into themselves (PBO_MOD_STATIC).
#include "ModApi.hpp"
#include "ModHooks.hpp"

#include "MatrixMap.hpp"
#include "MatrixObjectBuilding.hpp"
#include "MatrixRobot.hpp"
#include "Effects/MatrixEffect.hpp"
#include "Effects/MatrixEffectWeapon.hpp"
#include "Effects/MatrixEffectElevatorField.hpp"
#include "random.hpp"

#include <algorithm>

namespace RepairMod {
namespace {

constexpr int REPAIR_BEAM_LINGER = 500;  // ms a beam with nothing to repair stays on

// Points of the repair factory's model (Matrix\Building\b5.cvo, which has no named points): the tips of its two probes
// either side of the hatch in the front of its body, and the hatch; the pad in front is round its point (0, 0).
const D3DXVECTOR3 repair_probes[2] = {D3DXVECTOR3(-22, -28, 20), D3DXVECTOR3(22, -28, 20)};
const D3DXVECTOR3 repair_hatch(0, -24, 14);

// The repair factory's patient: the nearest damaged robot of its side in its capture area.
struct SFindRepairPatient {
    CMatrixMapStatic *found = nullptr;
    float dist2 = CAPTURE_RADIUS * CAPTURE_RADIUS;
    int side = 0;
};

bool FindRepairPatient(const D3DXVECTOR2 &center, CMatrixMapStatic *ms, uintptr_t user) {
    SFindRepairPatient *data = (SFindRepairPatient *)user;
    if (ms->GetSide() != data->side || !ms->NeedRepair() || !ms->IsLiveRobot())
        return true;
    const D3DXVECTOR2 to = center - *(D3DXVECTOR2 *)&ms->GetGeoCenter();
    const float dist2 = D3DXVec2LengthSq(&to);
    if (dist2 < data->dist2) {
        data->found = ms;
        data->dist2 = dist2;
    }
    return true;
}

// The original game's repair factory did nothing. With RepairPlant it repairs a robot of its side standing in its
// capture area (its pad) with two repairer's beams from its probes, each twice a repairer's rate (a robot with four
// repairers); the reinforcements' lift field reaches from its hatch to the robot meanwhile (presentation only).
void repair_takt(CMatrixBuilding &factory, int cms) {
    for (auto *&weapon : factory.m_RepairWeapon) {
        if (weapon && weapon->GetSideStorage() != factory.m_Side) {  // captured: the beams are the new side's
            factory.StopRepair();
            weapon->Release();
            weapon = nullptr;
        }
        if (!weapon) {
            weapon = (CMatrixEffectWeapon *)CMatrixEffect::CreateWeapon(factory.GetGeoCenter(), D3DXVECTOR3(0, 0, 1), 0, nullptr,
                                                                      WEAPON_REPAIR);
            weapon->SetOwner(&factory);
            weapon->ModifyCoolDown(-0.5f);
        }
    }
    SFindRepairPatient data;
    data.side = factory.m_Side;
    g_MatrixMap->FindObjects(factory.m_Pos, CAPTURE_RADIUS, 1, TRACE_ROBOT, &factory, FindRepairPatient, (uintptr_t)&data);
    // Beams with nothing more to repair stay a little (a robot under fire is damaged again at once) instead of going
    // out and starting over.
    factory.m_RepairIdle = data.found ? 0 : std::min(factory.m_RepairIdle + cms, REPAIR_BEAM_LINGER + 1);
    if (factory.m_RepairIdle > REPAIR_BEAM_LINGER) {
        factory.StopRepair();
        return;
    }
    if (data.found) {
        const D3DXVECTOR3 patient = data.found->GetGeoCenter();
        for (int i = 0; i < 2; ++i) {
            D3DXVECTOR3 from, dir;
            D3DXVec3TransformCoord(&from, &repair_probes[i], &factory.GetMatrix());
            dir = patient - from;
            D3DXVec3Normalize(&dir, &dir);
            factory.m_RepairWeapon[i]->Modify(from, dir, D3DXVECTOR3(0, 0, 0));
            factory.m_RepairWeapon[i]->FireBegin(D3DXVECTOR3(0, 0, 0), &factory);
        }
        D3DXVECTOR3 hatch;
        D3DXVec3TransformCoord(&hatch, &repair_hatch, &factory.GetMatrix());
        const float radius = data.found->GetRadius();
        const D3DXVECTOR3 forward = data.found->AsRobot()->m_Forward;
        if (factory.m_RepairField) {
            factory.m_RepairField->UpdateData(hatch, patient, radius, forward);
        }
        else {
            random::CosmeticScope cosmetic;  // presentation only: never what the battle computes
            factory.m_RepairField = (CMatrixEffectElevatorField *)CMatrixEffect::CreateElevatorField(hatch, patient, radius, forward);
            if (!g_MatrixMap->AddEffect(factory.m_RepairField))
                factory.m_RepairField = nullptr;
        }
    }
    else {
        factory.RemoveRepairField();
    }
    for (auto *weapon : factory.m_RepairWeapon)
        if (weapon->IsFire())
            weapon->Takt(float(cms));
}

}  // namespace

void init(ModApi::Context &) { ModHooks::hooks().repair_takt = repair_takt; }

}  // namespace RepairMod

PBO_MOD_ENTRY("repair", RepairMod::init)
