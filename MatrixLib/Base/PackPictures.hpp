// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <algorithm>

// A resource pack's picture in place of the game's (CFile::PackRoots): how it loads so that everything the game
// measures in the original's pixels (interface pieces, effect frames, the cursor's frames) stays where it was.
// The picture is the original's size times a power of two (2x, 4x... or a half, a quarter): the texture is as large
// as the file, and the game is told the original's size (Plan::shift). A picture of another size is stretched to the
// nearest such multiple. The game keeps an original whose sides are not powers of two at the top left of a texture
// rounded up to powers of two, pixel for pixel, and measures it by that texture: a stretched one goes to the same
// place of a texture 2^shift times as large (Plan::in_place).
namespace PackPictures {

constexpr unsigned most_side = 4096;  // a texture's side at most (larger ones are made smaller until they fit)

struct Plan {
    bool use = true;          // false: the game's own picture
    bool stretch = false;     // load it as width x height (else as the file is)
    bool in_place = false;    // (stretch) at the top left of a texture of texture_side(original, shift)
    unsigned width = 0, height = 0;
    int shift = 0;            // the texture is the original's size times 2^shift: the game's size is the texture's / 2^shift
};

inline bool power_of_two(unsigned v) { return v != 0 && (v & (v - 1)) == 0; }
inline unsigned rounded_up(unsigned v) {
    unsigned t = 1;
    while (t < v) t <<= 1;
    return t;
}
inline unsigned times(unsigned v, int k) { return k >= 0 ? v << k : v >> -k; }
// The side of the texture holding an original's side (rounded up to a power of two) at 2^shift.
inline unsigned texture_side(unsigned original, int shift) { return std::max(1u, times(rounded_up(original), shift)); }

// w x h: the pack's picture; ow x oh: the original's (0 x 0: the game has none: the pack's as it is).
inline Plan plan(unsigned w, unsigned h, unsigned ow, unsigned oh, unsigned most = most_side) {
    Plan p;
    if (w == 0 || h == 0) {
        p.use = false;
        return p;
    }
    if (ow == 0 || oh == 0) {  // nothing to keep: only not too large
        while ((w >> -p.shift) > most || (h >> -p.shift) > most) --p.shift;
        if (p.shift < 0) {
            p.stretch = true;
            p.width = std::max(1u, w >> -p.shift);
            p.height = std::max(1u, h >> -p.shift);
        }
        p.shift = 0;  // (its own size is the one the game knows)
        return p;
    }
    // An original of other sides is only made larger (a smaller copy would not keep its pixels whole).
    const bool pow2 = power_of_two(ow) && power_of_two(oh);
    const int least = pow2 ? -4 : 0;
    auto fits = [&](int k) { return texture_side(ow, k) <= most && texture_side(oh, k) <= most; };
    auto stretched = [&](int k) {
        p.shift = k;
        p.stretch = true;
        p.in_place = !pow2;
        p.width = times(ow, k);
        p.height = times(oh, k);
        return p;
    };
    // The original's size times 2^k, k from -4 to 4: as the file is.
    for (int k = -4; k <= 4; ++k) {
        const bool exact = k >= 0 ? (ow << k) == w && (oh << k) == h : (w << -k) == ow && (h << -k) == oh;
        if (!exact) continue;
        if (fits(k)) {
            p.shift = k;
            return p;
        }
        int c = k;  // too large: the largest multiple that fits
        while (c > least && !fits(c)) --c;
        if (!fits(c)) {
            p.use = false;
            return p;
        }
        return stretched(c);
    }
    // Another size: the multiple nearest to the picture (of two as near, the larger) that fits.
    auto off = [](unsigned a, unsigned b) { return a > b ? a - b : b - a; };
    int k = 0;
    bool found = false;
    unsigned long long best = ~0ull;
    for (int c = least; c <= 4; ++c) {
        const unsigned cw = times(ow, c), ch = times(oh, c);
        if (cw == 0 || ch == 0 || !fits(c)) continue;
        const unsigned long long d = (unsigned long long)off(cw, w) + off(ch, h);
        if (d <= best) {
            best = d;
            k = c;
            found = true;
        }
    }
    if (!found) {
        p.use = false;
        return p;
    }
    return stretched(k);
}

// The game's size of a texture of `side` loaded by a plan's shift.
inline unsigned game_side(unsigned side, int shift) {
    return shift >= 0 ? std::max(1u, side >> shift) : side << -shift;
}

}  // namespace PackPictures
