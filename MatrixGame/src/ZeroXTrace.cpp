// MatrixGame - licensed under GPLv2 or any later version.
#include "ZeroXTrace.hpp"

namespace ZeroXTrace {
namespace {
// BUILD_EXE is defined for this file only in the standalone configuration.
#ifdef BUILD_EXE
constexpr bool build_default = false;
#else
constexpr bool build_default = true;
#endif
bool original_trace = build_default;
}  // namespace

void set_original(bool original) {
    original_trace = original;
}

bool original() {
    return original_trace;
}

bool original_by_default() {
    return build_default;
}

}  // namespace ZeroXTrace
