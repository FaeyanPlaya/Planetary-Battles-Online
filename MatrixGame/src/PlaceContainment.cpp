// MatrixGame - licensed under GPLv2 or any later version.
#include "PlaceContainment.hpp"

namespace PlaceContainment {
namespace {
// BUILD_EXE is defined for this file only in the standalone configuration.
#ifdef BUILD_EXE
constexpr bool build_default = false;
#else
constexpr bool build_default = true;
#endif
bool original_containment = build_default;
}  // namespace

void set_original(bool original) {
    original_containment = original;
}

bool original() {
    return original_containment;
}

bool original_by_default() {
    return build_default;
}

}  // namespace PlaceContainment
