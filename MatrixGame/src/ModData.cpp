// MatrixGame - licensed under GPLv2 or any later version.
#include "ModData.hpp"
#include "utils.hpp"

#include <cwchar>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

using Base::CBlockPar;

namespace ModData {
namespace {

// One change to the game's data and how to take it off; taken off in the reverse order.
struct Change {
    enum class Kind { Param, Block, Blocks } kind = Kind::Param;
    CBlockPar *block = nullptr;
    std::wstring name;                // Param: its name
    bool existed = false;             // Param: whether the game had it
    std::wstring old;                 // Param: the game's value
    CBlockPar *added = nullptr;       // Block: the block added
    std::vector<std::pair<std::wstring, std::unique_ptr<CBlockPar>>> blocks;  // Blocks: the game's blocks there
};

std::vector<Change> g_changes;

// As mod.txt: UTF-16 LE with its BOM, else UTF-8 (a BOM skipped); empty when it is neither.
std::wstring decode(std::string_view bytes) {
    if (bytes.size() >= 2 && bytes[0] == '\xff' && bytes[1] == '\xfe') {
        std::wstring text;
        for (std::size_t i = 2; i + 1 < bytes.size(); i += 2)
            text += wchar_t(std::uint8_t(bytes[i]) | (std::uint8_t(bytes[i + 1]) << 8));
        return text;
    }
    if (bytes.starts_with("\xef\xbb\xbf"))
        bytes.remove_prefix(3);
    try {
        return utils::to_wstring(bytes);
    }
    catch (...) {
        return {};
    }
}

std::wstring with_folder(std::wstring value, const std::wstring &folder) {
    for (size_t at = value.find(mod_folder); at != std::wstring::npos; at = value.find(mod_folder, at + folder.size()))
        value.replace(at, std::wcslen(mod_folder), folder);
    return value;
}

int param_index(CBlockPar &block, const std::wstring &name) {
    for (int i = 0; i < block.ParCount(); ++i)
        if (block.ParGetName(i) == name)
            return i;
    return -1;
}

int block_index(CBlockPar &block, const CBlockPar *child) {
    for (int i = 0; i < block.BlockCount(); ++i)
        if (block.BlockGet(i) == child)
            return i;
    return -1;
}

// A copy of `from` into the empty `to`, %MOD% replaced.
void copy(CBlockPar &to, CBlockPar &from, const std::wstring &folder) {
    for (int i = 0; i < from.ParCount(); ++i)
        to.ParAdd(from.ParGetName(i), with_folder(from.ParGet(i), folder));
    for (int i = 0; i < from.BlockCount(); ++i)
        copy(*to.BlockAdd(from.BlockGetName(i)), *from.BlockGet(i), folder);
}

void take_off(Change &c) {
    switch (c.kind) {
        case Change::Kind::Param: {
            if (c.existed)
                c.block->ParSetAdd(c.name, c.old);
            else if (const int i = param_index(*c.block, c.name); i >= 0)
                c.block->ParDelete(i);
            break;
        }
        case Change::Kind::Block:
            if (const int i = block_index(*c.block, c.added); i >= 0)
                c.block->BlockDelete(i);
            break;
        case Change::Kind::Blocks:
            while (c.block->BlockCount())
                c.block->BlockDelete(0);
            for (auto &[name, saved] : c.blocks)
                c.block->BlockAdd(name)->CopyFrom(*saved);
            break;
    }
}

void blocks_below(CBlockPar &block, std::unordered_set<const CBlockPar *> &out) {
    for (int i = 0; i < block.BlockCount(); ++i) {
        out.insert(block.BlockGet(i));
        blocks_below(*block.BlockGet(i), out);
    }
}

// Before `game`'s blocks go (ReplaceBlocks): what earlier mods laid inside them, or added among them, is taken off,
// the latest first. Those changes point into blocks that are deleted here: taken off later (undo) they would write to
// freed blocks, and the blocks kept for undo would hold the earlier mods' values as if they were the game's.
void take_off_inside(CBlockPar &game) {
    std::unordered_set<const CBlockPar *> inside;
    blocks_below(game, inside);
    for (std::size_t i = g_changes.size(); i-- > 0;) {
        Change &c = g_changes[i];
        if (!inside.contains(c.block) && !(c.kind == Change::Kind::Block && c.block == &game))
            continue;
        take_off(c);
        g_changes.erase(g_changes.begin() + std::ptrdiff_t(i));
    }
}

void lay(CBlockPar &game, CBlockPar &mod, const std::wstring &folder) {
    const bool replace = mod.ParGetNE(replace_blocks).GetInt() == 1;
    for (int i = 0; i < mod.ParCount(); ++i) {
        const std::wstring name = mod.ParGetName(i);
        if (name == replace_blocks)
            continue;
        Change change;
        change.block = &game;
        change.name = name;
        change.existed = param_index(game, name) >= 0;
        if (change.existed)
            change.old = game.ParGet(name);
        game.ParSetAdd(name, with_folder(mod.ParGet(i), folder));
        g_changes.push_back(std::move(change));
    }
    if (replace) {
        take_off_inside(game);
        Change change;
        change.kind = Change::Kind::Blocks;
        change.block = &game;
        while (game.BlockCount()) {
            auto saved = std::make_unique<CBlockPar>();
            saved->CopyFrom(*game.BlockGet(0));
            change.blocks.emplace_back(game.BlockGetName(0), std::move(saved));
            game.BlockDelete(0);
        }
        g_changes.push_back(std::move(change));
        for (int i = 0; i < mod.BlockCount(); ++i)
            copy(*game.BlockAdd(mod.BlockGetName(i)), *mod.BlockGet(i), folder);
        return;
    }
    for (int i = 0; i < mod.BlockCount(); ++i) {
        const std::wstring name = mod.BlockGetName(i);
        if (CBlockPar *there = game.BlockGetNE(name)) {
            lay(*there, *mod.BlockGet(i), folder);
            continue;
        }
        Change change;
        change.kind = Change::Kind::Block;
        change.block = &game;
        change.name = name;
        change.added = game.BlockAdd(name);
        copy(*change.added, *mod.BlockGet(i), folder);
        g_changes.push_back(std::move(change));
    }
}

}  // namespace

void undo() {
    while (!g_changes.empty()) {
        take_off(g_changes.back());
        g_changes.pop_back();
    }
}

bool apply(CBlockPar &data, const std::vector<std::filesystem::path> &files) {
    undo();
    bool all = true;
    for (const auto &file : files) {
        std::ifstream in(file, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!in && !in.eof()) {
            all = false;
            continue;
        }
        CBlockPar mod;
        try {
            mod.LoadFromText(decode(bytes));
        }
        catch (...) {
            all = false;
            continue;
        }
        lay(data, mod, file.parent_path().wstring());
    }
    return all;
}

}  // namespace ModData
