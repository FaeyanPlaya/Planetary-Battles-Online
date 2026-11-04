// MatrixGame - licensed under GPLv2 or any later version.
// Небо высокого разрешения (High resolution sky), a personal mod: newsky.dll in mods\newsky. The game's skies at 2048
// from Universe's PBNewSky (by Klaxons), read in place from the person's game where it has the mod
// (MapPacks::new_sky_folder); nothing of it is shipped. Without it: the game's skies. The tests build this file into
// themselves (PBO_MOD_STATIC).
#include "ModApi.hpp"
#include "ModHooks.hpp"

#include "MapPacks.hpp"

namespace NewSkyMod {
namespace {

std::map<std::wstring, std::wstring> sky_sides(const std::wstring &sky) {
    const auto folder = MapPacks::new_sky_folder();
    if (folder.empty())
        return {};
    return MapPacks::new_sky(folder, sky);
}

}  // namespace

void init(ModApi::Context &) { ModHooks::hooks().sky_sides = sky_sides; }

}  // namespace NewSkyMod

PBO_MOD_ENTRY("newsky", NewSkyMod::init)
