// MatrixGame - licensed under GPLv2 or any later version.
// Дальняя прорисовка (Far view), a personal mod: farview.dll in mods\farview. The view, the fog, the effects and the
// objects drawn reach the person's FarViewScale= times as far (the settings' row: 1.5 to 3); shadows to where the fog
// begins; the strategy camera's near clip plane follows its height, so the depth buffer keeps telling the
// ground from what lies on it far away. Drawing only: the battle never reads it. The tests build this file into
// themselves (PBO_MOD_STATIC).
#include "ModApi.hpp"
#include "ModHooks.hpp"

#include <algorithm>

namespace FarViewMod {
namespace {

float view_scale(float chosen) { return chosen; }

// Shadows to where the fog begins (FOG_NEAR_K of the view), as in the original (1024 of 4000): drawn into the fog they
// stayed dark, not fogged, specks in the haze.
float shadow_distance(float view_distance) { return view_distance * 0.5f; }

// 0.15 of the camera's height above the ground: nothing comes nearer the camera than the ground under it, flyers fly
// lower than the camera's least height, and the depth buffer's precision grows with the near plane.
float near_plane(float height) { return std::max(1.0f, height * 0.15f); }

}  // namespace

void init(ModApi::Context &) {
    auto &hooks = ModHooks::hooks();
    hooks.view_scale = view_scale;
    hooks.shadow_distance = shadow_distance;
    hooks.near_plane = near_plane;
}

}  // namespace FarViewMod

PBO_MOD_ENTRY("farview", FarViewMod::init)
