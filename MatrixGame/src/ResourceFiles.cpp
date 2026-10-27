// MatrixGame - licensed under GPLv2 or any later version.
#include "ResourceFiles.hpp"

#include "CFile.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace Startup {

bool ResourceFileExists(const std::filesystem::path &path) {
    return std::filesystem::is_regular_file(path);
}

AudioPackages OpenPackages(const std::filesystem::path &data, bool music) {
    const auto robots = (data / L"robots.pkg").wstring();
    const auto sound = (data / L"sound.pkg").wstring();
    const auto voices = (data / L"voices.pkg").wstring();
    const auto tracks = (data / L"music.pkg").wstring();

    AudioPackages packages{ResourceFileExists(sound), ResourceFileExists(voices), music && ResourceFileExists(tracks)};
    Base::CFile::AddPackFile(robots.c_str());
    if (packages.sound) Base::CFile::AddPackFile(sound.c_str());
    if (packages.voices) Base::CFile::AddPackFile(voices.c_str());
    if (packages.music) Base::CFile::AddPackFile(tracks.c_str());

    const std::vector<std::wstring> failed = Base::CFile::OpenPackFiles();
    const auto opened = [&failed](const std::wstring &name) {
        return std::find(failed.begin(), failed.end(), name) == failed.end();
    };
    packages.sound = packages.sound && opened(sound);
    packages.voices = packages.voices && opened(voices);
    packages.music = packages.music && opened(tracks);
    return packages;
}

} // namespace Startup
