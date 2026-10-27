// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "StandaloneSettings.hpp"

#include <cstdint>
#include <string>

class CBitmap;

class CMatrixHint;

// In-game settings dialog for the standalone game. It replaces the battle menu while open,
// keeps the battle paused and returns to the menu when applied or canceled.
namespace SettingsWindow {

// True in the standalone game; the DLL host owns its settings.
bool available();
bool active();
void open();
// Shows the window again with its unsaved edits.
void resume();
// Applies or discards the edits and returns to the battle menu.
void close(bool apply);

// Input while the dialog is active. Each returns true when the event was consumed.
bool keyboard(uint8_t vk, bool down);
void mouse_move(int x, int y);
bool mouse_down(int x, int y);

// Battle menu integration: enables the menu's settings entry and handles its click area.
void customize_menu(std::wstring &menu_template);
// Repaints the settings icon of the composed battle menu like the menu's buttons: the game drew
// it as an unavailable (dark teal) entry.
void paint_menu_icon(CBitmap &menu);
bool menu_mouse_down(int x, int y);
void menu_mouse_move(int x, int y);
// Releases the menu highlight; called whenever a dialog closes.
void menu_closed();
// Re-centres the open window and drops the menu highlight after the rendering size changes.
void relayout();
// Releases dialog state before the session's hints and heap are destroyed.
void shutdown();

// The same window over the startup menu, before any battle: its own hint with text buttons. Apply
// saves the settings, switches the display and gives them to `values`, which the next battle uses.
void open_startup(StandaloneSettings::Values &values);
bool startup_active();
void startup_close(bool apply);
bool startup_keyboard(uint8_t vk, bool down);
void startup_mouse_move(int x, int y);
bool startup_mouse_down(int x, int y);
void startup_reset_input();

}  // namespace SettingsWindow
