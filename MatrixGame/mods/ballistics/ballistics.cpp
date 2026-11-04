// MatrixGame - licensed under GPLv2 or any later version.
// Баллистика ракет (Missile ballistics), a battle mod: ballistics.dll in mods\ballistics. A missile's (a robot's or a
// missile turret's) engine burns out when it is as far from where it was fired as its weapon reaches; it then goes on
// unpowered, slowed by the air and pulled down, and nothing steers it. It hurts whatever it hits at any distance (the
// original hurt only within the weapon's reach) and bursts only on hitting something (the original burst in the air
// 1500 ahead when it locked on nothing) (MissileBallistics). The tests build this file into themselves
// (PBO_MOD_STATIC).
#include "ModApi.hpp"
#include "ModHooks.hpp"

#include "MatrixMap.hpp"
#include "Effects/MatrixEffectMovingObject.hpp"

namespace BallisticsMod {
namespace {

// An unpowered missile's speed lost to the air (share per ms), its fall (speed per ms), the time (ms) after which one
// that hit nothing goes (the original's missile went after 10 s), and how far below the water's level it goes.
constexpr float MISSILE_DRAG = 0.0005f;
constexpr float MISSILE_GRAVITY = 0.003f;
constexpr int MISSILE_LIFETIME = 30000;
constexpr float MISSILE_BELOW = 100;

// The engine burns out as far from where the missile was fired as its weapon reaches.
void missile_burn(SMOProps &props) {
    auto flown = props.curpos - props.startpos;
    if (D3DXVec3LengthSq(&flown) >= props.common.hm.reach * props.common.hm.reach) {
        props.common.hm.burnt = 1;
        if (props.common.hm.target) {
            props.common.hm.target->Release();
            props.common.hm.target = NULL;
        }
    }
}

// Unpowered, slowed by the air and pulled down.
void missile_fall(SMOProps &props, float takt) {
    float keep = 1.0f - MISSILE_DRAG * takt;
    props.velocity *= keep > 0 ? keep : 0;
    props.velocity.z -= MISSILE_GRAVITY * takt;
}

// One fallen past the map's edge (nothing there to hit) goes below the water's level, which covers any ground lower.
bool missile_ends(const SMOProps &props) {
    return props.time > MISSILE_LIFETIME || (props.common.hm.burnt && props.curpos.z < WATER_LEVEL - MISSILE_BELOW);
}

bool missile_hurts_far() { return true; }

}  // namespace

void init(ModApi::Context &) {
    auto &hooks = ModHooks::hooks();
    hooks.missile_burn = missile_burn;
    hooks.missile_fall = missile_fall;
    hooks.missile_ends = missile_ends;
    hooks.missile_hurts_far = missile_hurts_far;
}

}  // namespace BallisticsMod

PBO_MOD_ENTRY("ballistics", BallisticsMod::init)
