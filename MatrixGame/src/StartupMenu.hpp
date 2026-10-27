// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "NetBattle.hpp"
#include "StandaloneSettings.hpp"
#include <string_view>

namespace StartupMenu {

void set_direct_start(bool enabled);
bool direct_start();

// The command line's --lobby-* options: the menu opens that lobby at once, and the lobby's battles take
// the --net-* test options.
void set_network(const NetBattle::Options &options);

// Runs on the existing device, before any battlefield or battle interface is constructed.
// Returns false for Exit/window close. Start updates values after successful validation and saving;
// a network battle started in the lobby also returns true, with NetBattle::prepared() set.
bool run(StandaloneSettings::Values &values);

}  // namespace StartupMenu
