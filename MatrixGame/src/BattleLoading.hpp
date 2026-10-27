// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <windows.h>
#include <string>
#include <string_view>

namespace BattleLoading {

// Independent of the renderer, game heap and map lifetime. A worker owns the
// loading window so it continues painting while the main thread loads or exits.
// False means the cosmetic screen could not be created; loading may continue.
bool show(HWND game_window, bool russian) noexcept;
void hide() noexcept;

// Private relaunch handshake. The old process keeps its loading screen until
// the new process presents a battle frame, reports a failure, or terminates.
std::wstring event_name();
bool valid_event_name(std::wstring_view name);
void connect(std::wstring_view name);
bool has_parent();
void presented(HRESULT result);
void failed() noexcept;
enum class WaitResult { Ready, ChildExited, Dismissed, TimedOut, Failed };
inline constexpr DWORD handoff_timeout_ms = 120000;
// Pumps the caller's messages. A supplied cancellation event is useful to non-UI callers.
WaitResult wait(HANDLE ready, HANDLE process, DWORD timeout_ms = handoff_timeout_ms, HANDLE cancel = nullptr);

} // namespace BattleLoading
