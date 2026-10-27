// MatrixGame - licensed under GPLv2 or any later version.
#include "DelayedDamage.hpp"
#include "Effects/MatrixEffectWeapon.hpp"

namespace DelayedDamage {
namespace {
// BUILD_EXE is defined for this file only in the standalone configuration.
#ifdef BUILD_EXE
constexpr bool build_default = false;
#else
constexpr bool build_default = true;
#endif
bool original_attribution = build_default;
}  // namespace

void set_original(bool original) {
    original_attribution = original;
}

bool original() {
    return original_attribution;
}

bool original_by_default() {
    return build_default;
}

int kept_side(EWeapon weapon, int stored_side) {
    if (!original_attribution && (weapon == WEAPON_ABLAZE || weapon == WEAPON_SHORTED))
        return stored_side;
    return 0;
}

}  // namespace DelayedDamage
