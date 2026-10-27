// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <filesystem>

namespace Startup {

// Standalone packages are real files, independent of the archive collection.
bool ResourceFileExists(const std::filesystem::path &path);

// Optional standalone audio packages that are open in the archive collection.
struct AudioPackages {
    bool sound{};
    bool voices{};
    bool music{};
    bool any() const { return sound || voices || music; }
};

// Registers robots.pkg and the optional audio packages present in data, then opens the collection.
// An optional package that cannot be opened is dropped and reported as missing; music.pkg is used only
// when music is set.
AudioPackages OpenPackages(const std::filesystem::path &data, bool music);

} // namespace Startup
