// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include "DisplaySettings.hpp"

#include <functional>

namespace Display {
void initialize(Settings settings);
void shutdown();
bool enabled();
bool changing();
Mode current_mode();
// Rendering size the controller holds: the back-buffer size, or the requested size while a lost device
// waits for ordinary recovery to apply it.
Size current_size();
bool supports_exclusive();
void apply_window(Mode mode);
void configure_parameters(D3DPRESENT_PARAMETERS &parameters);
HRESULT reset_device(const D3DPRESENT_PARAMETERS &parameters);
Mode fullscreen_mode();
// Queues a switch to target (applied by apply_pending) and sets the Alt+Enter fullscreen mode.
void request(Mode target, Mode fullscreen);
// Network battles and their lobby: Exclusive fullscreen loses the device whenever another window takes
// the screen (Alt+Tab, or another game window on the same PC), and a battle cannot step without it.
// While exclusive is not allowed every Exclusive request, setting and shortcut becomes Borderless, and
// a current Exclusive mode is switched to Borderless (by apply_pending). Kept across initialize.
void set_exclusive_allowed(bool allowed);
bool exclusive_allowed();
// Queues a change of the rendering size (applied by apply_pending together with any mode switch).
void request_size(int width, int height);
bool handle_shortcut(UINT message, WPARAM key, LPARAM flags);
void focus_lost();
void confine_cursor();
bool apply_pending(bool resources_ready, void (*release)(), void (*restore)());

// Device-dependent steps of a change that resets the device.
struct ResetSteps {
    std::function<void(Mode)> window;       // Applies a window mode for the current rendering size.
    std::function<HRESULT()> reset;         // Configures the parameters for the new size and resets.
    std::function<HRESULT()> restore_reset; // Restores the previous parameters and resets.
};
// Moves to target at size through a device reset. A rejected reset restores the previous mode and size
// and resets again. The rendering globals g_ScreenX/g_ScreenY change only after a successful reset; a lost
// device keeps the requested size for ordinary recovery.
HRESULT reset_to(Mode target, Size size, bool resources_ready, void (*release)(), void (*restore)(),
                 const ResetSteps &steps);
void map_mouse(UINT message, int &x, int &y);
} // namespace Display
