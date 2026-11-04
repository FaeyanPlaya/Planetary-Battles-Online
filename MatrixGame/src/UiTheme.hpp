// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstdint>

// The look of this project's windows (the startup menu, the lobby, the settings, a mod's window, the wait between
// battles): light, the game's own grey panel, or dark, the same panel recoloured navy as SR HD's mods menu. The
// person chooses in the settings (Theme=); System follows Windows' own app mode.
namespace UiTheme {

enum class Choice { System, Light, Dark };

// The look from now on (System: Windows' app mode as it is now).
void choose(Choice choice);
bool dark();
void set_dark(bool dark);
// Windows' app mode is dark (HKCU ...\Themes\Personalize AppsUseLightTheme = 0).
bool system_dark();

// The windows' frame: the game's large panel (CMatrixHint makes both from its border 2), the dark one recoloured.
constexpr int light_border = 5;
constexpr int dark_border = 6;
inline int border() { return dark() ? dark_border : light_border; }

// A colour of the light look, or its dark twin.
inline uint32_t pick(uint32_t light, uint32_t dark_colour) { return dark() ? dark_colour : light; }

// The dark look's colours, on the navy panel.
namespace Dark {
constexpr uint32_t text = 0xFFD5E1E8;
constexpr uint32_t selected = 0xFF54C6F0;  // SR HD's cyan
constexpr uint32_t disabled = 0xFF5E6B75;
constexpr uint32_t arrow = 0xFF8798A5;
constexpr uint32_t legend = 0xFF9DADB8;
constexpr uint32_t group = 0xFF6FA8CC;
constexpr uint32_t ready = 0xFF6FD67F;
constexpr uint32_t error = 0xFFFF6E61;
constexpr uint32_t slow = 0xFFF0A848;
}  // namespace Dark

// The panel's picture (BGRA or BGR, `bytes` a pixel) recoloured: its grey parts (the panel, its stripes and lines,
// half of them at the rims' edges) become navy, its coloured and dark parts (the cyan light, the metal rims) stay.
void darken_panel(uint8_t *pixels, int width, int height, int pitch, int bytes);

}  // namespace UiTheme
