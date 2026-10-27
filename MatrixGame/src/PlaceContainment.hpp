// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

// The containment test of CMatrixMapLogic::FindPlace, which maps a movement cell to the place whose
// four-cell square contains it.
namespace PlaceContainment {

// The original game bounded the cell's X coordinate by the place's Y coordinate plus four, so
// off-diagonal places missed their own cells and matched cells outside their square. The standalone
// game corrects the bound by default; a DLL build keeps the original behaviour, because the host owns
// its settings.
void set_original(bool original);
bool original();
// The mode a process starts with: corrected in the standalone EXE, original in the DLL.
bool original_by_default();

}  // namespace PlaceContainment
