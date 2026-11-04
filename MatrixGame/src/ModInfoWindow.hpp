// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "ModInfo.hpp"

#include <functional>

// A mod's window (ModInfo) shown over the screen's middle, above the window that opened it; or a question of it.
namespace ModInfoWindow {

void show(const ModInfo::Info &info, ModInfo::Language language, double scale);
// Asks; `answer` is called with yes (true) or no once one is chosen (after the window is gone).
void ask(const ModInfo::Question &question, ModInfo::Language language, double scale, std::function<void(bool)> answer);
void close();  // a question: as no
bool open();
// Over the window that opened it again, after that window made its hint anew.
void raise();
// A click on the screen while it is open: a mod's window closes at any, a question only at its Yes or No.
void click(int x, int y);
// A key while it is open: Esc or Enter closes a mod's window; a question takes Enter or Y as yes, Esc or N as no.
void key(int vk);

}  // namespace ModInfoWindow
