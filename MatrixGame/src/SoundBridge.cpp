// MatrixGame - licensed under GPLv2 or any later version.
#include "SoundBridge.hpp"
#include "SoundMapping.hpp"
#include "MatrixGameDll.hpp"
#include "CFile.hpp"
#include "CBlockPar.hpp"
#include <stupid_logger.hpp>
#include <utils.hpp>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <filesystem>

namespace SoundBridge {
namespace {
std::unique_ptr<Audio::Service> standalone;
// Declared after the service so static destruction also releases the music stream first.
std::unique_ptr<Audio::Music> music;
float master = 1.0f;
float music_setting = 1.0f;
size_t next_track = 0;  // Each session starts with the track after the last one played.
// initialize_standalone created the service, so it can be recreated after a device error.
bool recreatable = false;
int recoveries = 0;  // Recreations after device errors since initialize_standalone.
constexpr int recovery_limit = 3;
constexpr wchar_t music_folder[] = L"music\\BATTLE";

void report(const std::wstring &name, const std::string &message) {
    lgr.error("Audio {}: {}")(utils::from_wstring(name), message);
}

float normalized(float value) { return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f; }

std::wstring folded(std::wstring text) {
    for (auto &ch : text) ch = static_cast<wchar_t>(std::towlower(ch));
    return text;
}
Audio::Mapping sound_mapping() {
    auto mapping = Audio::default_sound_mapping();
    try {
        if (!std::filesystem::exists(L"CFG\\sounds.txt")) return mapping;
        Base::CBlockPar config;
        config.LoadFromTextFile(L"CFG\\sounds.txt");
        if (config.BlockCount(L"Sound") != 1) throw std::runtime_error("Expected one Sound block");
        const auto *sounds = config.BlockGetNE(L"Sound");
        if (sounds->BlockCount()) throw std::runtime_error("Nested sound mappings are unsupported");
        Audio::Mapping overrides;
        for (int i = 0; i < sounds->ParCount(); ++i) {
            const std::wstring name = sounds->ParGetName(i);
            const std::wstring path = sounds->ParGet(i);
            const bool valid_name = !name.empty() && std::all_of(name.begin(), name.end(), [](wchar_t c) {
                return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
                       (c >= L'0' && c <= L'9') || c == L'_';
            });
            if (!valid_name || path.empty()) throw std::runtime_error("Invalid sound name or empty resource path");
            if (!overrides.emplace(L"Sound." + name, path).second)
                throw std::runtime_error("Duplicate sound override");
        }
        // Validate the entire override before changing any default.
        for (auto &[name, path] : overrides) mapping.insert_or_assign(name, std::move(path));
    } catch (const Base::CException &) {
        lgr.warning("Cannot read CFG/sounds.txt; using default sound mappings");
    } catch (const std::exception &error) {
        lgr.warning("Ignoring CFG/sounds.txt; using default sound mappings: {}")(error.what());
    }
    return mapping;
}

std::vector<uint8_t> load(const std::wstring &path, uint32_t limit) {
    try {
        Base::CFile file(path);
        file.OpenRead();
        const auto length = file.Size();
        if (length == 0 || length > limit)
            throw std::runtime_error("Sound resource has an invalid size");
        std::vector<uint8_t> bytes(length);
        file.Read(bytes.data(), length);
        return bytes;
    } catch (const Base::CException &) {
        throw std::runtime_error("Cannot read sound resource");
    }
}

std::vector<uint8_t> load_clip(const std::wstring &path) { return load(path, 16 * 1024 * 1024); }
#ifdef MATRIXGAME_MUSIC
// Compressed tracks stay in memory while they play; the largest original track is about 10.5 MB.
std::vector<uint8_t> load_track(const std::wstring &path) { return load(path, 64 * 1024 * 1024); }
#endif
} // namespace

std::vector<std::wstring> merge_tracks(std::vector<std::wstring> packed, const std::vector<std::wstring> &loose) {
    auto names = std::move(packed);
    names.insert(names.end(), loose.begin(), loose.end());
    std::ranges::stable_sort(names, [](const std::wstring &a, const std::wstring &b) { return folded(a) < folded(b); });
    names.erase(std::unique(names.begin(), names.end(),
                            [](const std::wstring &a, const std::wstring &b) { return folded(a) == folded(b); }),
                names.end());
    for (auto &name : names) name = std::wstring(music_folder) + L"\\" + name;
    return names;
}

std::vector<std::wstring> battle_tracks() {
    std::vector<std::wstring> loose;
    std::error_code error;
    for (std::filesystem::directory_iterator it(std::filesystem::path(music_folder), error), end;
         !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error)) loose.push_back(it->path().filename().wstring());
    }
    return merge_tracks(Base::CFile::PackedFileNames(music_folder), loose);
}

bool available() { return g_RangersInterface != nullptr || standalone != nullptr; }
namespace {
void replace(std::unique_ptr<Audio::Service> service) {
    // Music plays through the previous service's device, so it ends with that service.
    if (music) next_track = music->next_track();
    music.reset();
    standalone = std::move(service);
    if (standalone) standalone->master_volume(master);
}

// Creates the standalone device, effects and music; reports a failure and leaves audio disabled.
void start(uint64_t first_id) {
    try {
        auto mapping = sound_mapping();
        replace(std::make_unique<Audio::Service>(Audio::create_xaudio_device(), std::move(mapping), load_clip,
                                                 report, first_id));
        recreatable = true;
        lgr.notice("Standalone sound initialized");
#ifdef MATRIXGAME_MUSIC
        auto tracks = battle_tracks();
        if (tracks.empty()) {
            lgr.notice("Battle music disabled: no tracks in DATA/music.pkg");
            return;
        }
        lgr.notice("Battle music: {} tracks")(tracks.size());
        install_music(std::move(tracks), load_track, Audio::open_vorbis);
#else
        // Built with MATRIXGAME_MUSIC=OFF: no decoder, so tracks are not even listed.
        lgr.notice("Battle music disabled: this build has no music decoder");
#endif
    } catch (const Base::CException &) {
        lgr.error("Standalone sound disabled: cannot initialize audio resources");
    } catch (const std::exception &error) {
        lgr.error("Standalone sound disabled: {}")(error.what());
    }
}
} // namespace

void install(std::unique_ptr<Audio::Service> service) {
    recreatable = false;
    replace(std::move(service));
}
void install_music(std::vector<std::wstring> tracks, Audio::Loader loader, Audio::DecoderFactory open) {
    music.reset();
    if (!standalone || tracks.empty()) return;
    music = std::make_unique<Audio::Music>(standalone->device(), std::move(tracks), next_track, std::move(loader),
                                           std::move(open), report);
    music->volume(music_setting);
}
void shutdown() { install(nullptr); }

void initialize_standalone(bool resources_available) {
    shutdown();
    recoveries = 0;
    if (resources_available) start(1);
}

bool recover() {
    if (!standalone) return false;
    const std::string error = standalone->device().critical_error();
    if (error.empty()) return false;
    // Handles of the failed service stay unused, so they cannot reach a sound of the new one.
    const uint64_t first_id = standalone->next_id();
    const float level = music ? music->level() : 1.0f;
    const bool owned = recreatable;
    shutdown();
    if (!owned) {
        lgr.error("Standalone sound disabled: {}")(error);
        return true;
    }
    if (recoveries >= recovery_limit) {
        lgr.error("Standalone sound disabled after {} device errors: {}")(recoveries + 1, error);
        return true;
    }
    ++recoveries;
    lgr.error("Audio device stopped: {}; recreating standalone sound")(error);
    start(first_id);
    if (music) music->level(level);
    return true;
}

uint32_t create(wchar_t *name, int group, int loop) {
    if (g_RangersInterface) return g_RangersInterface->m_SoundCreate(name, group, loop);
    if (!standalone) return 0;
    const auto id = standalone->create(name, loop != 0);
    return id == Audio::invalid_id ? 0 : id;
}
void destroy(uint32_t id) {
    if (g_RangersInterface) g_RangersInterface->m_SoundDestroy(id);
    else if (standalone) standalone->destroy(id);
}
void play(uint32_t id) {
    if (g_RangersInterface) g_RangersInterface->m_SoundPlay(id);
    else if (standalone) standalone->play(id);
}
bool playing(uint32_t id) {
    if (g_RangersInterface) return g_RangersInterface->m_SoundIsPlay(id) != 0;
    return standalone && standalone->playing(id);
}
void volume(uint32_t id, float value) {
    if (g_RangersInterface) g_RangersInterface->m_SoundVolume(id, value);
    else if (standalone) standalone->volume(id, value);
}
void pan(uint32_t id, float value) {
    if (g_RangersInterface) g_RangersInterface->m_SoundPan(id, value);
    else if (standalone) standalone->pan(id, value);
}
void master_volume(float value) {
    master = normalized(value);
    if (standalone) standalone->master_volume(master);
}
void update_music() {
    if (music) music->update();
}
void music_volume(float value) {
    music_setting = normalized(value);
    if (music) music->volume(music_setting);
}
float music_volume() { return music_setting; }
bool music_available() { return g_RangersInterface != nullptr || music != nullptr; }
float music_level() {
    if (g_RangersInterface) return g_RangersInterface->m_MusicVolumeGet();
    return music ? music->level() : 0.0f;
}
void music_level(float value) {
    if (g_RangersInterface) g_RangersInterface->m_MusicVolumeSet(value);
    else if (music) music->level(value);
}
float volume(uint32_t id) {
    if (g_RangersInterface) return g_RangersInterface->m_SoundGetVolume(id);
    return standalone ? standalone->volume(id) : 0.0f;
}
} // namespace SoundBridge
