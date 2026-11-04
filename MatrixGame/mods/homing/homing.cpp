// MatrixGame - licensed under GPLv2 or any later version.
// Самонаведение ракет (Homing missiles), a battle mod: homing.dll in mods\homing. A missile (a robot's or a missile
// turret's) flies straight for HOMING_DELAY, then seeks the enemy unit most ahead within 45 degrees (the original: 10
// degrees, and only nudged its course; bases and factories too) and keeps it while it is ahead, turning to it at
// HOMING_TURN_RATE (HomingMissiles). The tests build this file into themselves (PBO_MOD_STATIC).
#include "ModApi.hpp"
#include "ModHooks.hpp"

#include "MatrixMap.hpp"
#include "MatrixMapStatic.hpp"
#include "Effects/MatrixEffectMovingObject.hpp"

namespace HomingMod {
namespace {

// The cone a missile seeks a target in (cos 45 degrees; the original's is 10), how long after the launch it flies
// straight (ms; Universe's PBHomingMissiles: 500 a robot's, 200 a turret's), and how fast it then turns: the share of
// the way to its target's direction per ms (0.65 of the first version's 0.006: Universe's homing speed, the middle of
// a robot's 0.5 and a turret's 0.8, taking 1 as the first version). The sphere ahead it seeks in: a radius of this
// many of its weapon's reaches, so its diameter is two (the original's radius: HOMING_RADIUS).
constexpr float HOMING_SEEK_COS = 0.70710678f;
constexpr float HOMING_SEEK_REACHES = 1;
constexpr float HOMING_DELAY = 400;
constexpr float HOMING_TURN_RATE = 0.0039f;

// The target is kept while it lives and is ahead.
void homing_keep(SMOProps &props, const D3DXVECTOR3 &dir) {
    bool keep = props.common.hm.target->m_Object != NULL;
    if (keep) {
        auto to = props.common.hm.target->m_GeoCenter - props.curpos;
        keep = D3DXVec3Dot(&dir, &to) > 0;
    }
    if (keep) {
        props.target = props.common.hm.target->m_GeoCenter;
    }
    else {
        props.common.hm.target->Release();
        props.common.hm.target = NULL;
    }
}

// Robots, flyers and turrets only: a base or factory ahead no longer draws the missile off its course.
void homing_seek(const SMOProps &props, float &radius, float &min_cos, int &mask) {
    if (props.common.hm.reach > 0)
        radius = HOMING_SEEK_REACHES * props.common.hm.reach;
    min_cos = HOMING_SEEK_COS;
    mask &= ~TRACE_BUILDING;
}

// It seeks only without a target, and not before HOMING_DELAY.
bool homing_waits(const SMOProps &props) { return props.common.hm.target || props.time < HOMING_DELAY; }

// After HOMING_DELAY it turns to its target every takt (the original only nudged its course as it sought), and speeds
// up as the original.
void homing_turn(SMOProps &props, const D3DXVECTOR3 &way, float takt) {
    const float dtime = 0.1f * takt;
    D3DXVECTOR3 to = way;
    if (props.common.hm.target) {
        auto tmp = props.target - props.curpos;
        D3DXVec3Normalize(&to, &tmp);
    }
    float turn = props.time < HOMING_DELAY ? 0 : HOMING_TURN_RATE * takt;
    D3DXVECTOR3 dir = way + (to - way) * (turn < 1 ? turn : 1);
    D3DXVec3Normalize(&dir, &dir);
    props.velocity = dir * (D3DXVec3Length(&props.velocity) + 0.1f * dtime);
}

}  // namespace

void init(ModApi::Context &) {
    auto &hooks = ModHooks::hooks();
    hooks.homing_keep = homing_keep;
    hooks.homing_seek = homing_seek;
    hooks.homing_waits = homing_waits;
    hooks.homing_turn = homing_turn;
}

}  // namespace HomingMod

PBO_MOD_ENTRY("homing", HomingMod::init)
