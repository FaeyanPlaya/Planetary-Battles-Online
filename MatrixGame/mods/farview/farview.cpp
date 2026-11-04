// MatrixGame - licensed under GPLv2 or any later version.
// Дальняя прорисовка (Far view), a personal mod: farview.dll in mods\farview. The view, the fog, the effects and the
// objects drawn reach the person's FarViewScale= times as far (the settings' row: 1.5 to 3), shadows 1.5 times that.
// Drawing only: the battle never reads it. The tests build this file into themselves (PBO_MOD_STATIC).
#include "ModApi.hpp"
#include "ModHooks.hpp"

namespace FarViewMod {
namespace {

float view_scale(float chosen) { return chosen; }

// The original drew shadows to 1024; the far view, half as far again as its scale.
float shadow_distance(float scale) { return 1536.0f * scale; }

}  // namespace

void init(ModApi::Context &) {
    auto &hooks = ModHooks::hooks();
    hooks.view_scale = view_scale;
    hooks.shadow_distance = shadow_distance;
}

}  // namespace FarViewMod

PBO_MOD_ENTRY("farview", FarViewMod::init)
