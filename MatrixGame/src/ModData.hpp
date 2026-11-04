// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "CBlockPar.hpp"

#include <filesystem>
#include <vector>

// A battle mod's data (data.txt in its folder, in the game's own data format) laid over the game's data
// (g_MatrixData) for a battle, and taken off again before the next one (games/space-rangers-hd/MODS-DESIGN.md):
// - name=value sets the game's parameter of that name in that block (or adds it);
// - name { ... } goes into the game's block of that name (or adds it);
// - ReplaceBlocks=1 in a block: its blocks take the place of all the game's blocks there (the game's parameters
//   there stay), as reinforcement templates do;
// - %MOD% in a value is the mod's folder.
namespace ModData {

inline constexpr const wchar_t *file_name = L"data.txt";
inline constexpr const wchar_t *replace_blocks = L"ReplaceBlocks";
inline constexpr const wchar_t *mod_folder = L"%MOD%";

// Takes off what the last call laid, then lays these files over `data`, in order (a later file wins). A file that
// cannot be read is left out (false).
bool apply(Base::CBlockPar &data, const std::vector<std::filesystem::path> &files);
// Takes off what is laid.
void undo();

}  // namespace ModData
