// MatrixGame - licensed under GPLv2 or any later version.
#include "LogFolder.hpp"
#include "ExecutablePath.hpp"

#include <ctime>
#include <cwchar>
#include <system_error>

namespace LogFolder {

// Beside the EXE, wherever it was started from (the log opens before the game makes the EXE's folder current).
std::filesystem::path folder() {
    try {
        return Startup::ExecutablePath().parent_path() / name;
    }
    catch (...) {
        return name;
    }
}

std::filesystem::path file(const std::wstring &file) {
    const std::filesystem::path logs = folder();
    std::error_code error;
    std::filesystem::create_directories(logs, error);  // (if it cannot be made, opening the file says so)
    return logs / file;
}

std::wstring stamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    wchar_t text[32] = {};
    std::wcsftime(text, std::size(text), L"%Y-%m-%d_%H-%M-%S", &local);
    return text;
}

}  // namespace LogFolder
