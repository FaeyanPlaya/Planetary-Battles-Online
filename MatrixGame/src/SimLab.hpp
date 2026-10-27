// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "BattleRules.hpp"

#include <cstdint>
#include <optional>
#include <string>

// Standalone determinism laboratory. Active only when CFG/simlab.txt exists.
// It runs one battle with every side under AI control, a fixed seed and a fixed
// integer frame step, and writes a world-state hash trace for run comparison.
namespace SimLab {

enum class CameraPolicy { Fixed, Orbit };

struct Options {
    bool active = false;
    std::wstring map;           // bare name resolves under Matrix\Map\, as on the command line
    std::uint32_t seed = 1;
    int step_ms = 10;
    int duration_ms = 60000;
    int hash_every_ms = 1000;
    bool dump = false;          // per-entity lines at every hash point
    CameraPolicy camera = CameraPolicy::Fixed;
    int camera_cell = -1;       // 0..8: the camera stays over that point of the orbit tour (rendering checks)
    std::wstring trace = L"simlab-trace.txt";
    int trace_draws_until_ms = -1;  // log every simulation draw with its stack up to this game time
    int trace_draws_from_ms = 0;
    // Hash and dump after every step between these game times (divergence hunts); -1: off.
    int detail_from_ms = -1;
    int detail_until_ms = -1;
    // Sides driven by people, bit per side id; 0: every side AI. People's sides act through
    // commands; the player side (1) takes this PC's mouse and keyboard unless a replay runs.
    unsigned human_sides = 0;
    int view_side = 1;           // the side this client shows and controls (PLAYER_SIDE)
    bool no_pause = true;        // menus and the robot constructor do not pause the battle
    bool maintenance_ready = false;  // reinforcements callable at once (the start wait is 500 s)
    std::wstring record;         // write this run's keyboard/mouse messages per loop iteration
    std::wstring replay;         // feed a recorded input file instead of live input
    std::wstring command_record; // write every executed command with its game time
    std::wstring command_replay; // execute a command recording instead of live input; ends at its end
    std::wstring command_script; // extra scheduled commands (bots for other people's sides)
    // The battle rules read from the replayed record (command or input); none for older records.
    std::optional<BattleRules::Rules> rules;
    // Bonus=None,AirSupport,None,None (sides 1..4) and Resources=equal|map for a new battle.
    std::optional<std::array<BattleChoice::Bonus, BattleRules::side_count>> bonus;
    std::optional<BattleRules::Resources> resources;
    std::optional<int> ai_resources;  // AiResources=map|x1..x4
};

// Reads CFG/simlab.txt beside the executable. Absent file -> inactive options.
Options load(const wchar_t *path = L"CFG\\simlab.txt");

// Call after CGame::Init: forces full AI control and writes the trace header.
void begin(const Options &options);

// Runs the frame loop with a synthetic clock advancing step_ms per frame and
// stops after duration_ms of game time. Returns the L3GRun result.
int run(const Options &options);

// Canonical world-state digest (FNV-1a 64) of sides and live objects.
std::uint64_t world_hash(std::string *dump);

// Appends a gameplay event line to the trace while a lab run is active.
bool active();
void note(const std::string &event);

} // namespace SimLab
