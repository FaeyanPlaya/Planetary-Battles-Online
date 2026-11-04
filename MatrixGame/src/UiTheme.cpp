// MatrixGame - licensed under GPLv2 or any later version.
#include "UiTheme.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace UiTheme {
namespace {
bool g_dark = false;
}

void choose(Choice choice) { g_dark = choice == Choice::Dark || (choice == Choice::System && system_dark()); }

bool dark() { return g_dark; }

void set_dark(bool dark) { g_dark = dark; }

bool system_dark() {
    DWORD light = 1, size = sizeof(light);
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    return status == ERROR_SUCCESS && light == 0;
}

void darken_panel(uint8_t *pixels, int width, int height, int pitch, int bytes) {
    if (!pixels || bytes < 3) return;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            uint8_t *p = pixels + y * pitch + x * bytes;
            const float b = p[0], g = p[1], r = p[2];
            const float lum = 0.299f * r + 0.587f * g + 0.114f * b;
            const float sat = std::max({r, g, b}) - std::min({r, g, b});
            // Grey and light: all of it; towards the rims (darker) and the light (coloured) less, so no edge glows.
            const float w = std::clamp((lum - 90) / 60, 0.0f, 1.0f) * std::clamp((90 - sat) / 40, 0.0f, 1.0f);
            if (w <= 0) continue;
            // The panel's light greys squeezed into a navy's dark range; its own tint kept a little (the glow).
            const float d = 18 + std::max(0.0f, lum - 150) * 0.42f;  // the grey stripes a little lighter than the dark
            const float target[3] = {d * 1.0f + (b - lum) * 0.6f, d * 0.85f + (g - lum) * 0.6f, d * 0.62f + (r - lum) * 0.6f};
            for (int c = 0; c < 3; ++c)
                p[c] = uint8_t(std::lround(std::clamp(p[c] + (target[c] - p[c]) * w, 0.0f, 255.0f)));
        }
}

}  // namespace UiTheme
