// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include <Audio.hpp>
#include <Music.hpp>
#include <string>
#include <vector>

namespace SoundBridge {
bool available();
// Creates the standalone device when any audio package is present, with battle music when tracks exist.
void initialize_standalone(bool resources_available);
// Installs a service that recover cannot recreate; a device error releases it.
void install(std::unique_ptr<Audio::Service> service);
// Plays the tracks through the installed service's device, at the retained MusicVolume level.
void install_music(std::vector<std::wstring> tracks, Audio::Loader loader, Audio::DecoderFactory open);
void shutdown();
// Call once per frame before other audio. After a device error, recreates the standalone service and
// music with the retained levels, up to a few times per initialize_standalone, and otherwise disables
// standalone audio. Returns true when the service was replaced or released; its sound handles are then
// void and the caller must drop them.
bool recover();
uint32_t create(wchar_t *name, int group, int loop);
void destroy(uint32_t id);
void play(uint32_t id);
bool playing(uint32_t id);
void volume(uint32_t id, float value);
void pan(uint32_t id, float value);
float volume(uint32_t id);
// Standalone master level for effects and speech; retained across services. Host audio ignores it.
void master_volume(float value);

// Battle music track paths from the packaged and loose file names in music\BATTLE: case-insensitive
// name order, with one entry for names that differ only in case.
std::vector<std::wstring> merge_tracks(std::vector<std::wstring> packed, const std::vector<std::wstring> &loose);
// The tracks of the registered packages and the loose music\BATTLE folder.
std::vector<std::wstring> battle_tracks();
// Starts standalone battle music on the first call and keeps its queue filled; call once per frame.
void update_music();
// Standalone MusicVolume level; retained across services. Host audio ignores it.
void music_volume(float value);
float music_volume();
// Music level the engine fades: the host's music volume, or the standalone fade level.
bool music_available();
float music_level();
void music_level(float value);
} // namespace SoundBridge
