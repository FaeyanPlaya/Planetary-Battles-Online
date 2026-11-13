// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstdint>

// The interface fixes' HideInterface: its key (interface.HideUI, F1 unless changed on the keys page) hides the
// battle's interface and shows it again, to look at the battle (a shader pack's picture, a screenshot with F9): the
// panels, the minimap, the chat, the windows and the menus over the battle, the health bars, the pause's shade, the
// marks on the ground (selection rings, the order's target, the path, the resources' figures) and the cursor. While it
// is hidden the interface takes no pointer (clicks go to the battle; over a window, nowhere) and over a window no keys
// but its key, Esc (shows it again) and the screenshot's. Drawing and input only.
namespace InterfaceHide {

bool hidden();
// A key of the battle (before the windows'); true when taken.
bool key(uint8_t vk, bool down);
// The battle over: shown again.
void reset();

}  // namespace InterfaceHide
