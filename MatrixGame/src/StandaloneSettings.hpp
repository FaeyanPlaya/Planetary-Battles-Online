// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "DisplaySettings.hpp"
#include "BattleChoice.hpp"
#include "random.hpp"

#include <string>
#include <vector>

namespace Base { class CBlockPar; }

// Player-editable options stored in CFG/standalone.txt. Display and random-generator keys keep
// the contracts of Display::resolve and RandomConfiguration::resolve; other keys fall back
// individually to their defaults.
namespace StandaloneSettings {

enum class Skybox { Off, Low, High };
// Attribution of ablaze and shorted pulses; Original restores the original game's behaviour.
enum class DamageAttribution { Corrected, Original };
// Cached AI group counts after regrouping; Original restores the original game's behaviour.
enum class GroupCountMode { Corrected, Original };
// Place containment test used by AI bomb escape; Original restores the original game's behaviour.
enum class PlaceContainmentMode { Corrected, Original };
// Tracing of segments without X travel; Original restores the original game's behaviour.
enum class ZeroXTraceMode { Corrected, Original };

struct Values {
    Display::Settings display;
    random::Mode random = random::Mode::ParkMiller;
    int sound_volume = 100;  // Percent of the clip volume, 0-100.
    int music_volume = 100;  // Percent of the battle music volume, 0-100.
    int camera_speed = 100;  // Percent of the configured camera scroll speed.
    bool stencil_shadows = true;
    bool projected_shadows = true;
    Skybox skybox = Skybox::High;
    bool robot_shadows = true;
    DamageAttribution delayed_damage = DamageAttribution::Corrected;
    std::wstring map;  // Remembered battle map, Matrix\Map\<name>.cmap; empty selects Config/Map.
    BattleChoice::Bonus battle_bonus = BattleChoice::Bonus::None;
    GroupCountMode group_counts = GroupCountMode::Corrected;
    PlaceContainmentMode place_containment = PlaceContainmentMode::Corrected;
    ZeroXTraceMode zero_x_trace = ZeroXTraceMode::Corrected;
    bool operator==(const Values &) const = default;
};

constexpr int min_camera_speed = 25;
constexpr int max_camera_speed = 300;

struct Selection {
    Values values;
    std::vector<std::string> invalid;  // Keys that fell back to defaults.
};

Selection resolve(const Base::CBlockPar &options);
Selection load(const wchar_t *path = L"CFG\\standalone.txt");

// Rewrites the supported keys of a settings text. Comments, unknown keys, blocks and line order
// are preserved; repeated definitions of a supported key are removed and missing keys appended.
std::wstring update_text(const std::wstring &text, const Values &values);

// Replaces the file atomically. On failure the previous file is left unchanged.
bool save(const Values &values, std::string &error, const wchar_t *path = L"CFG\\standalone.txt");

const char *name(Skybox skybox);
const char *name(DamageAttribution attribution);
const char *name(GroupCountMode mode);
const char *name(PlaceContainmentMode mode);
const char *name(ZeroXTraceMode mode);

}  // namespace StandaloneSettings
