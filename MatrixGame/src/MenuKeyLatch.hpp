// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include <windows.h>

// Action keys require a new press after a dialog transition. Navigation keys
// keep their repeats. Each active form also observes releases routed to its dialog.
struct MenuKeyLatch {
    bool enter = false;
    bool escape = false;
    bool accept(unsigned key, bool down) {
        bool *held = key == VK_RETURN ? &enter : key == VK_ESCAPE ? &escape : nullptr;
        if (!held) return down;
        const bool accepted = down && !*held;
        *held = down;
        return accepted;
    }
};
