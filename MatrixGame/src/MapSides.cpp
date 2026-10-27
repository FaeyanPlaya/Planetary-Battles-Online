// MatrixGame - licensed under GPLv2 or any later version.
#include "MapSides.hpp"
#include "Common.hpp"

#include "CStorage.hpp"

#include <algorithm>
#include <bit>

namespace MapSides {
namespace {

constexpr int side_count = 4;

void add(unsigned &sides, std::span<const std::uint8_t> owners) {
    for (const std::uint8_t side : owners)
        if (side >= 1 && side <= side_count) sides |= 1u << side;
}

std::span<const std::uint8_t> column(Base::CStorage &storage, const wchar *table, const wchar *name) {
    Base::CDataBuf *buf = storage.GetBuf(table, name, Base::ST_BYTE);
    if (!buf || buf->GetArrayLength(0) == 0) return {};
    return {buf->GetFirst<std::uint8_t>(0), buf->GetArrayLength(0)};
}

// A small generator of its own: the draw must not depend on the library's. Each number is a mixed
// counter, so close seeds (consecutive clock seconds) still draw differently.
struct Draw {
    std::uint32_t state;
    // 0..n-1
    int below(int n) {
        state += 0x9E3779B9u;
        std::uint32_t z = state;
        z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
        z = (z ^ (z >> 13)) * 0xC2B2AE35u;
        z ^= z >> 16;
        return int((std::uint64_t(z) * std::uint64_t(n)) >> 32);
    }
};

}  // namespace

unsigned from_owners(std::span<const std::uint8_t> buildings, std::span<const std::uint8_t> robots) {
    unsigned sides = 0;
    add(sides, buildings);
    add(sides, robots);
    return sides;
}

unsigned read(const std::wstring &path) {
    try {
        Base::CStorage storage;
        if (!storage.Load(path.c_str())) return 0;
        return from_owners(column(storage, DATA_BUILDINGS, DATA_BUILDINGS_SIDE),
                           column(storage, DATA_ROBOTS, DATA_ROBOTS_SIDE));
    }
    catch (...) {
        return 0;
    }
}

int count(unsigned sides) { return std::popcount(sides); }

std::vector<int> assign(unsigned sides, int people) { return assign(sides, std::vector<int>(std::max(0, people), 0)); }

std::vector<int> assign(unsigned sides, const std::vector<int> &chosen) {
    std::vector<int> out(chosen.size(), 0);
    unsigned taken = 0;
    for (std::size_t i = 0; i < chosen.size(); ++i) {
        const int side = chosen[i];
        if (side >= 1 && side <= side_count && (sides & (1u << side)) && !(taken & (1u << side))) {
            out[i] = side;
            taken |= 1u << side;
        }
    }
    for (std::size_t i = 0; i < chosen.size(); ++i) {
        if (out[i]) continue;
        for (int side = 1; side <= side_count && !out[i]; ++side)
            if ((sides & (1u << side)) && !(taken & (1u << side))) {
                out[i] = side;
                taken |= 1u << side;
            }
        if (!out[i]) return {};
    }
    return out;
}

std::vector<int> resolve(unsigned sides, const std::vector<int> &chosen, std::uint32_t seed) {
    Draw draw{seed};
    std::vector<int> out(chosen.size(), 0);
    unsigned taken = 0;
    for (int side = 1; side <= side_count; ++side) {
        if (!(sides & (1u << side))) continue;
        std::vector<std::size_t> wanted;
        for (std::size_t i = 0; i < chosen.size(); ++i)
            if (chosen[i] == side) wanted.push_back(i);
        if (wanted.empty()) continue;
        out[wanted[std::size_t(draw.below(int(wanted.size())))]] = side;
        taken |= 1u << side;
    }
    for (std::size_t i = 0; i < chosen.size(); ++i) {
        if (out[i]) continue;
        std::vector<int> free;
        for (int side = 1; side <= side_count; ++side)
            if ((sides & (1u << side)) && !(taken & (1u << side))) free.push_back(side);
        if (free.empty()) return {};
        out[i] = free[std::size_t(draw.below(int(free.size())))];
        taken |= 1u << out[i];
    }
    return out;
}

}  // namespace MapSides
