// MatrixGame - licensed under GPLv2 or any later version.
#include "BattleRules.hpp"

#include "DelayedDamage.hpp"
#include "GroupCounts.hpp"
#include "PlaceContainment.hpp"
#include "ZeroXTrace.hpp"

#include <format>

namespace BattleRules {
namespace {

std::optional<Rules> g_prescribed;
Resources g_resources = Resources::Equal;
int g_ai_resources = 0;
std::array<BattleChoice::Bonus, side_count> g_bonus{};
bool g_manual_control = true;
bool g_manual_bonus = true;
bool g_dropped_ai = true;

const char *mode_name(bool original) { return original ? "original" : "corrected"; }

}  // namespace

const char *bonus_name(BattleChoice::Bonus bonus) {
    switch (bonus) {
        case BattleChoice::Bonus::AirSupport: return "AirSupport";
        case BattleChoice::Bonus::ReinforcedArmor: return "ReinforcedArmor";
        default: return "None";
    }
}

std::optional<BattleChoice::Bonus> parse_bonus(std::string_view value) {
    if (value == "None") return BattleChoice::Bonus::None;
    if (value == "AirSupport") return BattleChoice::Bonus::AirSupport;
    if (value == "ReinforcedArmor") return BattleChoice::Bonus::ReinforcedArmor;
    return std::nullopt;
}

bool bonuses_from_text(std::string_view value, std::array<BattleChoice::Bonus, side_count> &out) {
    std::array<BattleChoice::Bonus, side_count> read{};
    for (int i = 0; i < side_count; ++i) {
        const auto comma = value.find(',');
        if ((comma == std::string_view::npos) != (i == side_count - 1)) return false;
        const auto bonus = parse_bonus(value.substr(0, comma));
        if (!bonus) return false;
        read[i] = *bonus;
        value = comma == std::string_view::npos ? std::string_view{} : value.substr(comma + 1);
    }
    out = read;
    return true;
}

Rules active() {
    return {random::mode(), DelayedDamage::original(), GroupCounts::original(), PlaceContainment::original(),
            ZeroXTrace::original(), g_resources, g_ai_resources, g_bonus, g_manual_control, g_manual_bonus,
            g_dropped_ai};
}

void apply_modes(const Rules &rules) {
    DelayedDamage::set_original(rules.delayed_damage_original);
    GroupCounts::set_original(rules.group_counts_original);
    PlaceContainment::set_original(rules.place_containment_original);
    ZeroXTrace::set_original(rules.zero_x_trace_original);
}

void set_sides(const Rules &rules) {
    g_resources = rules.resources;
    g_ai_resources = rules.ai_resources;
    g_bonus = rules.bonus;
    g_manual_control = rules.manual_control;
    g_manual_bonus = rules.manual_bonus;
    g_dropped_ai = rules.dropped_ai;
}

Resources resources() { return g_resources; }

int ai_resources() { return g_ai_resources; }

bool manual_control() { return g_manual_control; }

bool manual_bonus() { return g_manual_bonus; }

bool dropped_ai() { return g_dropped_ai; }

BattleChoice::Bonus bonus(int side) {
    return side >= 1 && side <= side_count ? g_bonus[side - 1] : BattleChoice::Bonus::None;
}

void prescribe(const std::optional<Rules> &rules) { g_prescribed = rules; }

const std::optional<Rules> &prescribed() { return g_prescribed; }

std::string to_text(const Rules &rules) {
    std::string bonuses;
    for (const auto bonus : rules.bonus) bonuses += (bonuses.empty() ? "" : ",") + std::string(bonus_name(bonus));
    return std::format("rng={} delayed_damage={} group_counts={} place_containment={} zero_x_trace={} "
                       "resources={} ai_resources={} bonus={} manual={} manual_bonus={} dropped={}",
                       rules.random == random::Mode::LegacyCRT ? "LegacyCRT" : "ParkMiller",
                       mode_name(rules.delayed_damage_original), mode_name(rules.group_counts_original),
                       mode_name(rules.place_containment_original), mode_name(rules.zero_x_trace_original),
                       rules.resources == Resources::Map ? "map" : "equal",
                       rules.ai_resources > 0 ? std::format("x{}", rules.ai_resources) : std::string("map"), bonuses,
                       rules.manual_control ? "on" : "off", rules.manual_bonus ? "on" : "off",
                       rules.dropped_ai ? "ai" : "idle");
}

std::optional<Rules> from_text(std::string_view text) {
    Rules rules;
    constexpr std::string_view keys[] = {"rng",          "delayed_damage", "group_counts", "place_containment",
                                         "zero_x_trace", "resources",      "ai_resources", "bonus",
                                         "manual",       "manual_bonus",   "dropped"};
    constexpr std::size_t key_count = std::size(keys);
    constexpr std::size_t required = 8;  // the manual keys came later: records without them have both on
    bool seen[key_count] = {};
    bool *const modes[] = {nullptr, &rules.delayed_damage_original, &rules.group_counts_original,
                           &rules.place_containment_original, &rules.zero_x_trace_original};
    while (!text.empty()) {
        const auto space = text.find(' ');
        const std::string_view pair = text.substr(0, space);
        text = space == std::string_view::npos ? std::string_view{} : text.substr(space + 1);
        if (pair.empty()) continue;
        const auto equals = pair.find('=');
        if (equals == std::string_view::npos) return std::nullopt;
        const std::string_view key = pair.substr(0, equals), value = pair.substr(equals + 1);
        std::size_t i = 0;
        while (i < key_count && keys[i] != key) ++i;
        if (i == key_count || seen[i]) return std::nullopt;
        seen[i] = true;
        if (key == "rng") {
            if (value == "ParkMiller") rules.random = random::Mode::ParkMiller;
            else if (value == "LegacyCRT") rules.random = random::Mode::LegacyCRT;
            else return std::nullopt;
        }
        else if (key == "resources") {
            if (value == "equal") rules.resources = Resources::Equal;
            else if (value == "map") rules.resources = Resources::Map;
            else return std::nullopt;
        }
        else if (key == "ai_resources") {
            if (value == "map") rules.ai_resources = 0;
            else if (value.size() == 2 && value[0] == 'x' && value[1] >= '1' && value[1] <= '0' + max_ai_resources)
                rules.ai_resources = value[1] - '0';
            else return std::nullopt;
        }
        else if (key == "bonus") {
            if (!bonuses_from_text(value, rules.bonus)) return std::nullopt;
        }
        else if (key == "manual" || key == "manual_bonus") {
            if (value != "on" && value != "off") return std::nullopt;
            (key == "manual" ? rules.manual_control : rules.manual_bonus) = value == "on";
        }
        else if (key == "dropped") {
            if (value != "ai" && value != "idle") return std::nullopt;
            rules.dropped_ai = value == "ai";
        }
        else if (value == "corrected" || value == "original") {
            *modes[i] = value == "original";
        }
        else {
            return std::nullopt;
        }
    }
    for (std::size_t i = 0; i < required; ++i)
        if (!seen[i]) return std::nullopt;
    return rules;
}

}  // namespace BattleRules
