// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Sides of a battle map that a person can play: sides 1..4 that own a building or a robot when the
// battle starts (a side with only cannons can do nothing). A bit per side id, as in human side masks.
namespace MapSides {

unsigned from_owners(std::span<const std::uint8_t> buildings, std::span<const std::uint8_t> robots);
// Reads the map file; 0 when it cannot be read.
unsigned read(const std::wstring &path);
int count(unsigned sides);
// Sides of `people` persons in joining order: the lowest playable sides. Empty when there are too few.
std::vector<int> assign(unsigned sides, int people);
// With each person's chosen side (0: none): a playable choice that nobody before took is kept, the
// others get the lowest free playable sides in joining order. Empty when there are too few.
std::vector<int> assign(unsigned sides, const std::vector<int> &chosen);
// The lobby's draw at the start: a playable side chosen by one person is theirs; among several who chose
// one side the seed picks who keeps it; the others and those who chose none (0) get free playable sides
// at random. The same seed gives the same sides. Empty when there are too few.
std::vector<int> resolve(unsigned sides, const std::vector<int> &chosen, std::uint32_t seed);

}  // namespace MapSides
