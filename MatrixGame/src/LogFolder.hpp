// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <filesystem>
#include <string>

// What is written to look into a problem goes to the logs folder beside the EXE, not beside
// the EXE itself: test.log, crash.txt, the desync reports and each battle's commands.
namespace LogFolder {

inline constexpr const wchar_t *name = L"logs";

// `file` in the logs folder, which is made when missing.
std::filesystem::path file(const std::wstring &file);
// The local time now as "2026-11-10_12-30-05": a battle's files are named by its start, so none is written over.
std::wstring stamp();

}  // namespace LogFolder
