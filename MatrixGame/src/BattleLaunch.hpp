// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace BattleLaunch {
inline constexpr std::wstring_view selected_argument = L"--start-selected-battle";
struct Arguments {
    bool selected = false;
    std::wstring_view map;
    std::wstring_view event;
};

// Excludes argv[0]. Ordinary map arguments keep the visibility-tool contract,
// including its historical tolerance of additional arguments.
inline Arguments classify(std::span<const std::wstring_view> arguments) {
    if (arguments.empty()) return {};
    if (arguments[0] != selected_argument) return {false, arguments[0], {}};
    if (arguments.size() > 2) throw std::runtime_error("Invalid selected-battle arguments");
    return {true, {}, arguments.size() == 2 ? arguments[1] : std::wstring_view{}};
}

inline std::wstring command(std::wstring_view executable, std::wstring_view event) {
    if (executable.empty() || executable.find(L'"') != std::wstring_view::npos ||
        event.empty() || event.find(L'"') != std::wstring_view::npos)
        throw std::runtime_error("Invalid battle relaunch command");
    return L"\"" + std::wstring(executable) + L"\" " + std::wstring(selected_argument) + L" \"" + std::wstring(event) + L"\"";
}
} // namespace BattleLaunch
