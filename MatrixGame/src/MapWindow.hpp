// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "StandaloneSettings.hpp"

#include <cstdint>
#include <optional>

// Startup and in-game New battle dialog of the standalone game. It lists the installed maps with previews; choosing one
// saves it as the remembered map and starts the game again on it.
namespace MapWindow {

// Where the window returns when it is canceled.
enum class Origin {
    Startup,    // Before the battlefield exists; Cancel returns to the startup menu.
    Menu,       // Cancel returns to the battle menu.
    BattleEnd,  // After the statistics of a finished battle; Cancel exits the game as before.
};

// True in the standalone game; the DLL host owns map selection.
bool available();
bool active();

// Opens the selector before a battle or over the paused battlefield. Start saves only the map and bonus.
void open(Origin origin, const StandaloneSettings::Values &values);
// Opens the window after a finished battle with the current settings. Returns false when unavailable.
bool open_after_battle();
// Plays the selected map or returns to the window's origin.
void close(bool play);

// Input while the window is active. Each returns true when the event was consumed.
bool keyboard(uint8_t vk, bool down);
void reset_input();
bool mouse_down(int x, int y, bool double_click = false);
void mouse_wheel(int steps, int x, int y);
std::optional<StandaloneSettings::Values> take_startup_choice();
void customize_menu(std::wstring &menu_template);
bool menu_mouse_down(int x, int y);
bool menu_keyboard(uint8_t vk, bool down);

// Rebuilds an open window at the centre of the current screen after a resolution change.
void relayout();

// Releases the preview and window state before the session's hints and heap are destroyed.
void shutdown();

}  // namespace MapWindow
