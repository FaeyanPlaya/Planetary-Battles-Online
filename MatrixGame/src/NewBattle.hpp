// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstdint>
#include <string>

// The battle menu's New battle (in place of its Help button; the N key): the battle of this game alone ends and the
// game starts again at the lobby without a network (NetBattle::after_local_battle). Not in a battle shared by
// several people.
namespace NewBattle {

// True in the standalone game's battle of this PC alone.
bool available();
// The menu's Help button is named New battle (N) in the standalone game.
void customize_menu(std::wstring &menu_template);
// Leaves the battle for the lobby; false when not available.
bool leave();
// The menu's N and a click on its New battle button; true when consumed.
bool menu_keyboard(uint8_t vk, bool down);
bool menu_mouse_down(int x, int y);

}  // namespace NewBattle
