// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "StandaloneSettings.hpp"

#include <string>

// The remembered map (Map= in CFG\standalone.txt: the last one started from the lobby without a network), started
// directly, and forgotten when it fails to load.
namespace MapSelection {

// Returns the map to start instead of Config/Map: the remembered map when it can be read, otherwise
// an empty string. An unreadable remembered map is reported, forgotten in the settings file and in
// values, so the configured map starts.
std::wstring startup_map(StandaloneSettings::Values &values);

// True after startup_map selected the remembered map for this process.
bool remembered_map_started();

// Call just before the remembered map loads. Clears it in the settings file, so that a map that crashes
// or fails while loading leaves the configured map for the next start.
void begin_map_load();

// Call once battle initialization has succeeded. Writes the remembered map back, except for a demo map:
// demos run in full-auto mode, where the Map window cannot be reached, so they play only once.
void finish_map_load();

// True when battle initialization failed after begin_map_load and the settings file no longer names the
// remembered map. False when the failure came earlier or the file could not be written.
bool forget_remembered_map();

// Whether a map path names a demo, which the game runs in full-auto mode.
bool is_demo(const std::wstring &path);

}  // namespace MapSelection
