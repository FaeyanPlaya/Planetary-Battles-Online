// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

enum EWeapon : unsigned int;

// Attribution of ablaze and shorted damage pulses. A flamethrower or electric hit stores its
// attacking side, and each later pulse damages the target on behalf of the stored side.
namespace DelayedDamage {

// The original game clears the stored side when a pulse lands, so later pulses lose kill credit and
// friendly-fire scaling. The standalone game corrects this by default and keeps the side for every
// pulse; a DLL build keeps the original behaviour, because the host owns its settings.
void set_original(bool original);
bool original();
// The mode a process starts with: corrected in the standalone EXE, original in the DLL.
bool original_by_default();

// Side stored after a hit that does not start a status: pulses keep it unless the original
// behaviour is selected; every other weapon clears it.
int kept_side(EWeapon weapon, int stored_side);

}  // namespace DelayedDamage
