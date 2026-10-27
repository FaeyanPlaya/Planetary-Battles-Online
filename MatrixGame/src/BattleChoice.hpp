// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

namespace BattleChoice {

enum class Bonus { None, AirSupport, ReinforcedArmor };

// Apply only the two supported bonus fields. Friendly fire belongs to the existing battle contract.
template<class Difficulty>
void apply(Bonus bonus, Difficulty &difficulty) {
    if (bonus == Bonus::ReinforcedArmor) difficulty.k_damage_enemy_to_player *= 0.5f;
    if (bonus == Bonus::AirSupport) difficulty.k_time_before_maintenance *= 0.33f;
}

}  // namespace BattleChoice
