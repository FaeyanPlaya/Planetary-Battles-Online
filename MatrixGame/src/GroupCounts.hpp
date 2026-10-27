// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

// Cached logic-group counts after CMatrixSideUnit::Regroup splits or merges groups.
namespace GroupCounts {

// The original game counted every robot moved out of a split group as an addition to its source, and
// left a merged, emptied group with its former count until the next Regroup. The standalone game
// corrects both by default; a DLL build keeps the original behaviour, because the host owns its settings.
void set_original(bool original);
bool original();
// The mode a process starts with: corrected in the standalone EXE, original in the DLL.
bool original_by_default();

}  // namespace GroupCounts
