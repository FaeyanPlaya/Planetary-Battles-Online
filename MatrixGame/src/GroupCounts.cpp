// MatrixGame - licensed under GPLv2 or any later version.
#include "GroupCounts.hpp"

namespace GroupCounts {
namespace {
// BUILD_EXE is defined for this file only in the standalone configuration.
#ifdef BUILD_EXE
constexpr bool build_default = false;
#else
constexpr bool build_default = true;
#endif
bool original_counts = build_default;
}  // namespace

void set_original(bool original) {
    original_counts = original;
}

bool original() {
    return original_counts;
}

bool original_by_default() {
    return build_default;
}

}  // namespace GroupCounts
