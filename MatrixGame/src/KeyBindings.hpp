// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "KeyActions.hpp"

#include <array>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Key rebinding (18): every key a person may change is an action (KeyActions.hpp) listed here, the original's and
// the mods' alike, so the settings' keys page shows a new one once it is in actions(). The person's choices are kept
// in their own file, over the game's keys. A mod of the mods folder adds its own (mod.txt's Key.<Id>=, ModKey), kept
// as <mod>.<Id>.
namespace KeyBindings {

struct Keys {
    std::array<int, KA_LAST> engine{};
    // The mods' keys by <mod>.<Id>, also of mods not installed now (kept for when they are back).
    std::map<std::string, int> mods;
    int &operator[](std::size_t action) { return engine[action]; }
    int operator[](std::size_t action) const { return engine[action]; }
    int *data() { return engine.data(); }
    auto begin() { return engine.begin(); }
    auto end() { return engine.end(); }
    auto begin() const { return engine.begin(); }
    auto end() const { return engine.end(); }
    // A mod's key; -1: none.
    int mod(const std::string &id) const;
    bool operator==(const Keys &) const = default;
};

constexpr const wchar_t *file = L"CFG\\keys.txt";  // this person's, beside the EXE as standalone.txt

// The game's key names (KEY_Q, KEY_LEFT; AssignKey uses them) or a number; -1: none.
int code(const std::wstring &name);
std::wstring name(int code);
// As the keys page shows a key: Q, LEFT, F2, [ ...
std::wstring shown(int code);

// The engine's keys (the game's data may change them: AssignKey); Keys also the mods' own.
void defaults(int *keys);
Keys defaults();
// The game's keys before the person's (set once the game's config is read; the engine's until then).
const Keys &base();
void set_base(const int *keys);

enum class Group { Camera, Driving, Orders, Building, Mods };
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
// mod.txt's group name (Camera, Driving, Orders, Building, any case); any other: Mods.
Group group_of(std::wstring_view name);

// A key of a mod of the mods folder (Mods::installed sets them): on the keys page after its group's own.
struct ModAction {
    std::string id;  // <mod>.<Id>
    std::wstring english, russian;
    Group group = Group::Mods;
    int key = -1;  // the mod's default
    bool operator==(const ModAction &) const = default;
};
void set_mod_actions(std::vector<ModAction> actions);
std::span<const ModAction> mod_actions();
// The person's mod keys now (set at the start and by the keys page), what a mod's code asks for.
void set_mod_keys(const std::map<std::string, int> &keys);
// A mod's key now: the person's, else the mod's default; -1: none.
int mod_key(std::string_view id);

// The file's Id=KEY_NAME lines over `over`; lines it cannot take are left out (their ids in `invalid`). A <mod>.<Id>
// line is kept whether that mod is installed or not.
Keys load(const std::wstring &path, const Keys &over, std::vector<std::string> *invalid = nullptr);
// Writes the keys that differ from `over` (none: an empty list). False with `error` when it cannot.
bool save(const std::wstring &path, const Keys &keys, const Keys &over, std::string &error);

}  // namespace KeyBindings
