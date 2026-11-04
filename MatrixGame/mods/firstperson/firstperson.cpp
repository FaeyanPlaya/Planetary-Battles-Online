// MatrixGame - licensed under GPLv2 or any later version.
// Вид от первого лица (First person view), a personal mod: firstperson.dll in mods\firstperson. In a robot or a
// turret driven, the wheel turned in once more with the camera all the way in looks from it (the camera's own
// first-person view, CMatrixCamera::m_FirstPerson); a step out, or leaving it, goes back. Drawing only. The tests
// build this file into themselves (PBO_MOD_STATIC).
#include "ModApi.hpp"
#include "ModHooks.hpp"

#include "MatrixCamera.hpp"
#include "MatrixMap.hpp"
#include "MatrixSide.hpp"

namespace FirstPersonMod {
namespace {

bool step_in(CMatrixCamera &camera) {
    if (camera.ModeIndex() != CAMERA_INROBOT || camera.DistParam() > 0.25f)
        return false;
    camera.EnterFirstPerson();  // all the way in already
    return true;
}

bool keeps(const CMatrixCamera &, int mode) {
    return mode == CAMERA_INROBOT && g_MatrixMap->GetPlayerSide()->GetArcadedObject();
}

}  // namespace

void init(ModApi::Context &) {
    auto &hooks = ModHooks::hooks();
    hooks.first_person_in = step_in;
    hooks.first_person_keeps = keeps;
}

}  // namespace FirstPersonMod

PBO_MOD_ENTRY("firstperson", FirstPersonMod::init)
