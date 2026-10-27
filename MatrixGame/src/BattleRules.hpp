// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "BattleChoice.hpp"
#include "random.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>

// Rules that change how a battle plays out: the random generator, the original/corrected modes, the
// resource balance and every side's bonus. Every client of a shared battle and every replay of its
// record must run with the same rules. A person's settings (CFG/standalone.txt) choose them for their
// own battles; a record carries the rules it was played with, and later the host will send its rules
// (with each person's bonus) to the other people.
namespace BattleRules {

// Start resources and base income of the sides people play: Equal gives each of them the map's
// player economy (side 1's entry); Map keeps every side's own entry (maps favour the AI sides).
enum class Resources { Equal, Map };

constexpr int side_count = 4;  // sides 1..4

struct Rules {
    random::Mode random = random::Mode::ParkMiller;
    bool delayed_damage_original = false;
    bool group_counts_original = false;
    bool place_containment_original = false;
    bool zero_x_trace_original = false;
    Resources resources = Resources::Equal;
    // Economy of AI sides: 0 as the map says; 1..4 the map's player economy with the start resources
    // times this and the player's base income (the lobby's AI difficulty; no effect when every side
    // is a person's).
    int ai_resources = 0;
    std::array<BattleChoice::Bonus, side_count> bonus{};  // index side - 1; applies while a person plays it
    // People may take direct control of a robot ("enter" it). The driven robot's weapons reach x1.2 farther
    // and it runs x1.1 faster (the original game's bonus for it) unless manual_bonus is off.
    bool manual_control = true;
    bool manual_bonus = true;
    // A person whose connection failed or whom the host did not wait for: the AI plays their side until they are
    // back (on), or the side gets no new orders (off: its robots finish the orders they have, nothing is built).
    bool dropped_ai = true;
    bool operator==(const Rules &) const = default;
};

// Rules of the running battle: the generator and modes read back from the engine, the rest as set.
Rules active();
// Sets the original/corrected modes; the random generator is chosen when the battle seeds.
void apply_modes(const Rules &rules);
// Sets the resource balance and the bonuses for the battle about to load (the map reads them).
void set_sides(const Rules &rules);
Resources resources();
int ai_resources();
constexpr int max_ai_resources = 4;
BattleChoice::Bonus bonus(int side);
bool manual_control();
bool manual_bonus();
bool dropped_ai();

// Rules the next battle uses whatever the person's settings say (a host's choice, a record being
// replayed); the settings then neither choose nor change them. nullopt returns to the settings.
void prescribe(const std::optional<Rules> &rules);
const std::optional<Rules> &prescribed();

// One line of space-separated pairs: rng=ParkMiller delayed_damage=corrected group_counts=corrected
// place_containment=corrected zero_x_trace=corrected resources=equal ai_resources=map
// bonus=None,AirSupport,None,None manual=on manual_bonus=on dropped=ai   (ai_resources: map or x1..x4; the
// manual keys and dropped (ai or idle) may be missing, as in records from before them: on, ai)
std::string to_text(const Rules &rules);
// Reads to_text output; every key must be present once with a valid value.
std::optional<Rules> from_text(std::string_view text);
// None, AirSupport or ReinforcedArmor.
const char *bonus_name(BattleChoice::Bonus bonus);
std::optional<BattleChoice::Bonus> parse_bonus(std::string_view value);
// "None,AirSupport,None,None": one bonus per side 1..4; `out` is unchanged when the text is invalid.
bool bonuses_from_text(std::string_view value, std::array<BattleChoice::Bonus, side_count> &out);

}  // namespace BattleRules
