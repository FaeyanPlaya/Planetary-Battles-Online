// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

// Object and culling bounds of CMatrixMap::Trace for a segment with equal X endpoints and different Y
// endpoints.
namespace ZeroXTrace {

// The original game started such a segment's interval in each map group at zero length, so every object
// hit was rejected and the culling height ignored the segment's descent. The standalone game bounds the
// interval by the nearest hit so far by default; a DLL build keeps the original behaviour, because the
// host owns its settings. Trace reads the mode once per call.
void set_original(bool original);
bool original();
// The mode a process starts with: corrected in the standalone EXE, original in the DLL.
bool original_by_default();

}  // namespace ZeroXTrace
