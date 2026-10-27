// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

// Include last in a translation unit whose effects are purely visual. Every
// random helper used by that file then draws from the cosmetic stream, even when
// simulation code calls into the effect (for example a weapon moving its lightning),
// because such effects exist only near some viewer's camera.

#include "random.hpp"

namespace cosmetic_random {

inline double rnd(double from, double to) {
    random::CosmeticScope cosmetic;
    return ::RND(from, to);
}
inline float frnd(double x) {
    random::CosmeticScope cosmetic;
    return ::FRND(x);
}
inline float fsrnd(double x) {
    random::CosmeticScope cosmetic;
    return ::FSRND(x);
}
inline int irnd(int n) {
    random::CosmeticScope cosmetic;
    return ::IRND(n);
}

} // namespace cosmetic_random

#define RND(from, to) cosmetic_random::rnd(from, to)
#define FRND(x) cosmetic_random::frnd(x)
#define FSRND(x) cosmetic_random::fsrnd(x)
#define IRND(n) cosmetic_random::irnd(n)
