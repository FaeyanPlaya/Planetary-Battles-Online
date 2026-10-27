// MatrixGame - SR2 Planetary battles engine
// Copyright (C) 2012, Elemental Games, Katauri Interactive, CHK-Games
// Licensed under GPLv2 or any later version
// Refer to the LICENSE file included

#pragma once

#include <string>

std::wstring PathToOutputFiles(const wchar_t *dest);

// The standalone game keeps one output folder (the minimap cache) beside the EXE instead of the
// original game's Documents\SpaceRangersHD, which Space Rangers HD clears and which is the person's.
void SetOutputFolder(const wchar_t *dest, const std::wstring &path);
