// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <windows.h>
#include <filesystem>

namespace Startup {

using ModulePathQuery = DWORD (WINAPI *)(HMODULE, LPWSTR, DWORD);
std::filesystem::path ExecutablePath(ModulePathQuery query = GetModuleFileNameW);

}  // namespace Startup
