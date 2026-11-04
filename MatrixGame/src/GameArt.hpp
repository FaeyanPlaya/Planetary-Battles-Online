// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class CBitmap;

// Pictures of the person's own Space Rangers HD, read in place from its packages each start (nothing of them is
// copied or shipped): the buttons of its mods menu, which the lobby's and the settings' mods lists use. Without the
// game (or its files) the windows draw their own.
namespace GameArt {

// Pixels as 0xAARRGGBB, rows top down.
struct Image {
    int width = 0, height = 0;
    std::vector<uint32_t> argb;
    bool operator==(const Image &) const = default;
};

// A .gi picture (the game's format) of type 2: a layer of opaque 5-6-5 pixels, a layer of half-transparent ones and
// their 6-bit alpha, each layer rows of runs (a byte: n < 128 skips n pixels, 128 + n gives n pixels, 0 ends the
// row). Empty: another type, or broken.
std::optional<Image> decode_gi(std::string_view bytes);
// A file of a .pkg (the game's packages); empty when the package or the file cannot be read.
std::optional<std::string> pkg_file(const std::filesystem::path &pkg, const std::string &name);

enum class Button {
    SwitchOff,  // the options' round switch (FormOptions2\2SwitchN), as SR HD's mods menu shows a mod off
    SwitchOn,   // lit (2SwitchD)
    Info,       // the round "i" (Msg\2UserN) that opens a mod's window in SR HD's mods menu
};
// The game's picture of a button, read the first time from the install CFG\game-path.txt names; null: not found.
const Image *button(Button which);
// For tests: the folder of the Space Rangers HD install to read them from (an empty path: none).
void set_install(const std::filesystem::path &game);

// The button at size x size: the game's picture, else one drawn like it (a round button in a dark rim, lit blue when
// on; the "i" lit with the letter in white).
Image picture(Button which, int size);

// The picture resampled to width x height (bilinear, over premultiplied colours: no dark fringes), for the hints.
Image resized(const Image &image, int width, int height);
std::unique_ptr<CBitmap> bitmap(const Image &image);

}  // namespace GameArt
