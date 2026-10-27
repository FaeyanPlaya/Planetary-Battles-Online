// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "NetBattle.hpp"
#include "StandaloneSettings.hpp"

#include <cstdint>

// The network battle screens of the startup menu: the host's lobby, the screen where a person types
// the host's address, a client's view of the lobby and the screen of the person's name. Starting a battle prepares it in NetBattle and
// ends the startup menu; the game then loads it.
namespace LobbyWindow {

bool active();
// Opens the host's lobby; false when it cannot listen (another lobby has the port).
// options.lobby (tests): its address, map, start count and bonus.
bool open_host(const StandaloneSettings::Values &values, const NetBattle::Options &options);
// Opens the address screen; with options.lobby (tests) it connects to options' address at once.
void open_join(const StandaloneSettings::Values &values, const NetBattle::Options &options);
// Opens the screen where a person chooses their PlayerName; forced: the first start, it closes only
// with a name saved.
void open_name(bool forced);

// Every frame: the network, the maps' sides and the screen.
void takt();
// While the window is in the background: the network only (people keep joining).
void poll();

bool keyboard(uint8_t vk, bool down);
bool mouse_down(int x, int y);
void mouse_wheel(int steps, int x, int y);
void reset_input();
// Rebuilds an open window at the centre of the current screen after a resolution change.
void relayout();
// Closes the lobby (saying goodbye) and releases the window.
void shutdown();

// The name this PC plays under: one chosen in this run, else --lobby-name (tests: several copies on one
// PC), else the saved one.
std::wstring player_name(const NetBattle::Options &options);

// A battle was started: NetBattle::prepared() holds it.
bool started();

}  // namespace LobbyWindow
