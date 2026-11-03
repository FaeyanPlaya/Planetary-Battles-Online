// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "KeyActions.hpp"

#include <array>
#include <span>
#include <string>
#include <vector>

// Key rebinding (18): every key a person may change is an action (KeyActions.hpp) listed here, the original's and
// the mods' alike, so the settings' keys page shows a new one once it is in actions(). The person's choices are kept
// in their own file, over the game's keys.
namespace KeyBindings {

using Keys = std::array<int, KA_LAST>;

constexpr const wchar_t *file = L"CFG\\keys.txt";  // this person's, beside the EXE as standalone.txt

// The game's key names (KEY_Q, KEY_LEFT; AssignKey uses them) or a number; -1: none.
int code(const std::wstring &name);
std::wstring name(int code);
// As the keys page shows a key: Q, LEFT, F2, [ ...
std::wstring shown(int code);

// The engine's keys (the game's data may change them: AssignKey).
void defaults(int *keys);
Keys defaults();
// The game's keys before the person's (set once the game's config is read; the engine's until then).
const Keys &base();
void set_base(const int *keys);

enum class Group { Camera, Driving, Orders, Building };
struct Action {
    EKeyAction action;
    const char *id;  // the file's name for it
    const wchar_t *english;
    const wchar_t *russian;
    Group group;
};
// In the keys page's order, by group.
std::span<const Action> actions();
const wchar_t *group_name(Group group, bool russian);

// The file's Id=KEY_NAME lines over `over`; lines it cannot take are left out (their ids in `invalid`).
Keys load(const std::wstring &path, const Keys &over, std::vector<std::string> *invalid = nullptr);
// Writes the keys that differ from `over` (none: an empty list). False with `error` when it cannot.
bool save(const std::wstring &path, const Keys &keys, const Keys &over, std::string &error);

}  // namespace KeyBindings
