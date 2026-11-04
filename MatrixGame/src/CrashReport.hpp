// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <windows.h>

#include <cstdio>
#include <string>

// What ended the game, appended to crash.txt beside the EXE (the working directory), so that a crash that
// closes the game without a word still says where it happened.
namespace CrashReport {

inline constexpr const char *file_name = "crash.txt";

// A fatal fault: its code, where it happened (module+offset), the registers and the return-address candidates
// on the stack. Safe in an exception filter: no allocation and no logger.
void write_fault(FILE *out, const EXCEPTION_POINTERS &ep);
// A C++ exception that ended the game; what as given.
void write_exception(FILE *out, const char *what);

// Appended to file_name; false when it cannot be opened.
bool fault(const EXCEPTION_POINTERS &ep);
bool exception(const char *what);

// Inside a catch: the current exception's text (an engine CException's info, a std::exception's what).
std::string current_exception();

}  // namespace CrashReport
