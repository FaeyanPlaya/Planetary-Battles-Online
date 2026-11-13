// MatrixGame - licensed under GPLv2 or any later version.
#include "InterfaceHide.hpp"
#include "KeyBindings.hpp"
#include "Mods.hpp"

namespace InterfaceHide {
namespace {
bool g_hidden = false;
}

bool hidden() { return g_hidden && Mods::switches().hide_interface; }

bool key(uint8_t vk, bool down) {
    if (!Mods::switches().hide_interface) return false;
    const int own = KeyBindings::mod_key("interface.HideUI");
    if (own > 0 && vk == own) {
        if (down) g_hidden = !g_hidden;
        return true;
    }
    if (g_hidden && vk == 0x1B) {  // Esc: shown again (the menu it would open could not be seen)
        if (down) g_hidden = false;
        return true;
    }
    return false;
}

void reset() { g_hidden = false; }

}  // namespace InterfaceHide
