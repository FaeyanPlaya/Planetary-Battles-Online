// MatrixGame - licensed under GPLv2 or any later version.
#include "ExecutablePath.hpp"

#include <format>
#include <stdexcept>
#include <string>

namespace Startup {

std::filesystem::path ExecutablePath(ModulePathQuery query) {
    constexpr DWORD maximumCapacity = 32768;
    for (DWORD capacity = MAX_PATH; ; capacity = capacity > maximumCapacity / 2 ? maximumCapacity : capacity * 2) {
        std::wstring buffer(capacity, L'\0');
        const DWORD length = query(nullptr, buffer.data(), capacity);
        if (length == 0) {
            const DWORD error = GetLastError();
            throw std::runtime_error(std::format("GetModuleFileNameW failed (Win32 error={})", error));
        }
        if (length < capacity) {
            buffer.resize(length);
            return std::filesystem::path(buffer);
        }
        if (capacity == maximumCapacity) {
            throw std::runtime_error("GetModuleFileNameW path exceeds the supported 32767 characters");
        }
    }
}

}  // namespace Startup
