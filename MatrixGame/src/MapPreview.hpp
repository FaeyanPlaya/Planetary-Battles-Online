// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "MapCatalog.hpp"
#include "SettingsModel.hpp"

#include <string>

// The preview picture of a map for the map windows: decoded with D3DX, scaled into MapModel's preview
// box and registered as the runtime hint bitmap MapModel::preview_bitmap.
namespace MapPreview {

// The picture of a map: its own preview, else the minimap the game drew of it the last time it was
// played here (<exe>\Cache\<file>.<UniqID>), else none. That minimap is the ground alone: the map's
// bases, factories and robots are drawn on it in their sides' colours, as on the maps' own previews.
std::wstring source(const MapCatalog::Map &map);

// Shows the picture at `path` (an empty path: none) scaled into `box` (by default MapModel's preview box).
// A path that failed to decode is not retried until another one (or another box) is shown.
void show(const std::wstring &path);
void show(const std::wstring &path, SettingsModel::Resolution box);
// Size of the shown picture; zero when there is none.
SettingsModel::Resolution size();
// Also frees the pixels, which come from the tracked heap and must be released before its teardown.
void release();

}  // namespace MapPreview
