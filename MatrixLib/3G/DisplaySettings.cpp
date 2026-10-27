// MatrixGame - licensed under GPLv2 or any later version.
#include "DisplaySettings.hpp"
#include "CBlockPar.hpp"
#include "utils.hpp"

#include <filesystem>
#include <algorithm>
#include <string>

namespace Display {
namespace {
bool read(const Base::CBlockPar &options, const wchar_t *key, std::wstring &value) {
    if (options.ParCount(key) != 1) return false;
    value = utils::trim(options.ParGet(key));
    utils::to_lower(value);
    return true;
}

// A number of pixels, or "auto" (0): the primary display's size fitted into 1920x1080 (auto_size).
bool dimension(const std::wstring &value, int minimum, int &result) {
    if (value == L"auto") {
        result = 0;
        return true;
    }
    if (value.empty() || value.size() > 4) return false;
    int number = 0;
    for (wchar_t ch : value) {
        if (ch < L'0' || ch > L'9') return false;
        number = number * 10 + (ch - L'0');
    }
    if (number < minimum || number > 8192) return false;
    result = number;
    return true;
}

bool mode_value(const std::wstring &value, Mode &mode) {
    if (value == L"windowed") mode = Mode::Windowed;
    else if (value == L"borderless") mode = Mode::Borderless;
    else if (value == L"exclusive") mode = Mode::Exclusive;
    else return false;
    return true;
}
}

Selection resolve(const Base::CBlockPar &options) {
    Settings settings;
    std::wstring value;
    if (options.ParCount(L"ResolutionWidth") &&
        (!read(options, L"ResolutionWidth", value) || !dimension(value, 640, settings.width))) return {{}, false};
    if (options.ParCount(L"ResolutionHeight") &&
        (!read(options, L"ResolutionHeight", value) || !dimension(value, 480, settings.height))) return {{}, false};
    if (options.ParCount(L"WindowMode") &&
        (!read(options, L"WindowMode", value) || !mode_value(value, settings.mode))) return {{}, false};
    if (options.ParCount(L"FullscreenMode") &&
        (!read(options, L"FullscreenMode", value) || !mode_value(value, settings.fullscreen) ||
         settings.fullscreen == Mode::Windowed)) return {{}, false};
    // Starting fullscreen selects the same fullscreen mode for subsequent toggles.
    if (settings.mode != Mode::Windowed) settings.fullscreen = settings.mode;
    return {settings, true};
}

Selection load(const wchar_t *path) {
    try {
        Base::CBlockPar options;
        if (std::filesystem::exists(path)) options.LoadFromTextFile(path);
        return resolve(options);
    } catch (const Base::CException &) {
    } catch (const std::exception &) {
    }
    return {{}, false};
}

Size auto_size(Size screen) {
    constexpr int most_width = 1920, most_height = 1080;
    if (screen.width <= 0 || screen.height <= 0) return {most_width, most_height};
    if (screen.width <= most_width && screen.height <= most_height)
        return {std::max(screen.width, 640), std::max(screen.height, 480)};
    // A larger screen: its shape, as large as fits (2560x1440 and 3840x2160 give 1920x1080).
    const double k = std::min(double(most_width) / screen.width, double(most_height) / screen.height);
    return {std::max(int(screen.width * k), 640), std::max(int(screen.height * k), 480)};
}

const char *name(Mode mode) {
    switch (mode) {
        case Mode::Windowed: return "Windowed";
        case Mode::Borderless: return "Borderless";
        case Mode::Exclusive: return "Exclusive";
    }
    return "Unknown";
}

Mode toggle_target(Mode current, Mode fullscreen) {
    return current == Mode::Windowed ? fullscreen : Mode::Windowed;
}

Change plan(Mode current, Mode fullscreen, bool mode_pending, std::optional<Mode> requested, Size current_size,
            std::optional<Size> requested_size) {
    Change change;
    change.target = mode_pending ? requested.value_or(toggle_target(current, fullscreen)) : current;
    change.size = requested_size.value_or(current_size);
    change.resize = change.size != current_size;
    change.reset = change.resize || current == Mode::Exclusive || change.target == Mode::Exclusive;
    return change;
}

int map_coordinate(int position, int client_extent, int render_extent) {
    if (client_extent <= 1 || render_extent <= 1) return 0;
    const int result = static_cast<int>(static_cast<int64_t>(position) * (render_extent - 1) / (client_extent - 1));
    if (position < 0) return std::min(-1, result);
    if (position >= client_extent) return std::max(render_extent, result);
    return result;
}

bool Shortcut::handle(UINT message, WPARAM key, LPARAM flags, bool &request) {
    request = false;
    if (key != VK_RETURN) return false;
    if (message == WM_KEYUP || message == WM_SYSKEYUP) {
        const bool consumed = held;
        held = false;
        return consumed;
    }
    if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN) return false;
    const bool alt = (static_cast<uintptr_t>(flags) & (1u << 29)) != 0;
    const bool repeat = (static_cast<uintptr_t>(flags) & (1u << 30)) != 0;
    // A fresh ordinary Enter cannot belong to the previous Alt+Enter press.
    // Its key-up may have arrived while a display transition suppressed input.
    if (!alt && !repeat) {
        held = false;
        return false;
    }
    if (!held && !alt) return false;
    request = !held && !repeat;
    held = true;
    return true;
}
} // namespace Display
