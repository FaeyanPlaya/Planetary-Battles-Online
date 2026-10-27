// MatrixGame - licensed under GPLv2 or any later version.
#include "MapSelection.hpp"
#include "MapCatalog.hpp"
#include "ExecutablePath.hpp"
#include "BattleLoading.hpp"
#include "BattleLaunch.hpp"

#include "utils.hpp"

#include <windows.h>

#include <cwctype>
#include <memory>

#include <stupid_logger.hpp>

namespace MapSelection {
namespace {
bool remembered_started = false;
bool map_loading = false;
bool map_cleared = false;
std::wstring remembered;
bool relaunch_pending = false;

bool store_map(const std::wstring &map) {
    auto stored = StandaloneSettings::load().values;
    stored.map = map;
    std::string error;
    if (StandaloneSettings::save(stored, error)) return true;
    lgr.error("Cannot save CFG/standalone.txt: {}")(error);
    return false;
}
}  // namespace

bool is_demo(const std::wstring &path) {
    std::wstring lower = path;
    for (auto &ch : lower) ch = static_cast<wchar_t>(std::towlower(ch));
    return lower.find(L"demo") != std::wstring::npos;
}

std::wstring startup_map(StandaloneSettings::Values &values) {
    if (values.map.empty()) return {};
    if (MapCatalog::readable(values.map)) {
        remembered_started = true;
        remembered = values.map;
        lgr.notice("Starting the remembered map {}")(utils::from_wstring(values.map));
        return values.map;
    }
    lgr.error("The remembered map {} cannot be read; starting the configured map")(utils::from_wstring(values.map));
    const std::wstring message = L"The map " + values.map +
                                 L" chosen in the map selector cannot be loaded.\nThe game starts the configured map "
                                 L"instead. Choose another map in New battle.";
    values.map.clear();
    auto stored = StandaloneSettings::load().values;
    stored.map.clear();
    std::string error;
    if (!StandaloneSettings::save(stored, error)) lgr.error("Cannot save CFG/standalone.txt: {}")(error);
    MessageBoxW(nullptr, message.c_str(), L"Map unavailable", MB_OK | MB_ICONWARNING);
    return {};
}

bool remembered_map_started() { return remembered_started; }

void begin_map_load() {
    if (!remembered_started) return;
    map_loading = true;
    map_cleared = store_map({});
}

void finish_map_load() {
    if (!map_loading) return;
    map_loading = false;
    if (is_demo(remembered)) {
        lgr.notice("The demo map {} plays once; the next start uses the configured map")(utils::from_wstring(remembered));
        return;
    }
    store_map(remembered);
}

bool forget_remembered_map() {
    if (!map_loading) return false;
    map_loading = false;
    // Already cleared before loading; repeated in case that save failed.
    if (!map_cleared && !store_map({})) return false;
    lgr.notice("Forgot the remembered map after a failed start");
    return true;
}

bool choose(StandaloneSettings::Values values, const std::wstring &map, std::string &error) {
    values.map = map;
    if (!StandaloneSettings::save(values, error)) return false;
    lgr.notice("Remembered map {}; restarting the game")(utils::from_wstring(map));
    relaunch_pending = true;
    return true;
}

bool relaunch_requested() { return relaunch_pending; }

bool relaunch() {
    const auto executable = Startup::ExecutablePath();
    const auto event_name = BattleLoading::event_name();
    std::unique_ptr<void, decltype(&CloseHandle)> ready(CreateEventW(nullptr, TRUE, FALSE, event_name.c_str()), &CloseHandle);
    if (!ready || GetLastError() == ERROR_ALREADY_EXISTS) {
        BattleLoading::hide();
        lgr.error("Cannot create the battle-loading handshake");
        return false;
    }
    std::wstring command = BattleLaunch::command(executable.native(), event_name);
    const std::wstring directory = executable.parent_path().native();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, directory.c_str(),
                        &startup, &process)) {
        lgr.error("Cannot start the game again (Windows error {})")(GetLastError());
        BattleLoading::hide();
        return false;
    }
    CloseHandle(process.hThread);
    std::unique_ptr<void, decltype(&CloseHandle)> child(process.hProcess, &CloseHandle);
    const auto result = BattleLoading::wait(ready.get(), child.get());
    BattleLoading::hide();
    switch (result) {
        case BattleLoading::WaitResult::Ready: break;
        case BattleLoading::WaitResult::ChildExited: lgr.warning("The new game exited before the loading handoff"); break;
        case BattleLoading::WaitResult::Dismissed: lgr.warning("Loading screen dismissed; the new game may still be loading"); break;
        case BattleLoading::WaitResult::TimedOut: lgr.warning("Loading handoff timed out after 120 seconds; the new game may still be loading"); break;
        case BattleLoading::WaitResult::Failed: lgr.error("Loading handoff wait failed"); break;
    }
    return true;
}

}  // namespace MapSelection
