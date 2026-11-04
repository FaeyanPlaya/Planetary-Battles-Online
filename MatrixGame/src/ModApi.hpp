// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "PBOEngineId.hpp"

#include <filesystem>
#include <string>
#include <string_view>

// A mod's code: a DLL in its folder (mods/<id>/, mod.txt's Dll=), built against this engine's headers and linked
// with PBOEngine.dll, so it calls the engine's functions and reads its globals directly. One of its sources has
//     #include "ModApi.hpp"
//     void init(ModApi::Context &mod) { ... }   // registers its keys and hooks (ModHooks.hpp)
//     PBO_MOD_ENTRY("<id>", init)
// (A test program builds the same sources into itself with PBO_MOD_STATIC: the entry then runs init as the program
// starts, with the mod's id and mods/<id> as its folder.)
// The engine loads it only when it carries the engine's own PBO_ENGINE_ID (built against the same headers), so a
// mod is rebuilt for every build of the game. The mods are never sent over the network: a battle mod is the same
// files on every PC or the lobby refuses it (games/space-rangers-hd/MODS-DESIGN.md).
namespace ModApi {

// A key pressed in a battle (Windows' virtual-key code); true: the mod took it, the game does nothing more with it.
// A plain function: the mod's DLL stays loaded, and nothing of it is left to the engine's destructors.
using KeyHandler = bool (*)(int vk);

// What the engine tells a mod's init, and what the mod may ask of it.
class Context {
public:
    Context(std::string id, std::filesystem::path folder) : m_Id(std::move(id)), m_Folder(std::move(folder)) {}
    const std::string &id() const { return m_Id; }
    // The mod's folder: its data files are there.
    const std::filesystem::path &folder() const { return m_Folder; }
    // Whether the mod is on now: a battle mod in this battle, a personal one for this person.
    bool on() const;
    // A line in test.log, with the mod's id.
    void log(std::string_view text) const;
    // The key the person gave the mod's Key.<id> (mod.txt; the settings' keys page), a virtual-key code; -1: none.
    int key(std::string_view id) const;
    // The value of the mod's Setting.<id> (mod.txt; the settings' mods page): the person's, else its default; empty
    // for an id mod.txt lacks. Asked when needed, it is always the one now.
    std::wstring setting(std::string_view id) const;
    // Asked for every key pressed in a battle that the game's own keys did not take (the digits' groups come after;
    // not over a dialog, not while spectating), while the mod is on; the mods in the order they registered.
    void on_key(KeyHandler handler) const;

private:
    std::string m_Id;
    std::filesystem::path m_Folder;
};

// The engine's side: the key to the handlers of the mods on now, in turn; true when one took it.
bool key_down(int vk);
// A mod built into a test program (PBO_MOD_STATIC): its init, at once.
bool init_static(const char *id, void (*init)(Context &));
// Tests: forgets the handlers.
void clear_handlers();

}  // namespace ModApi

// A test may build a mod as if for another engine.
#ifndef PBO_MOD_ENGINE_ID
#define PBO_MOD_ENGINE_ID PBO_ENGINE_ID
#endif

// The DLL's two exports: the engine id it was built with and its init (called once, as the game starts).
#ifdef PBO_MOD_STATIC
#define PBO_MOD_ENTRY(id, init) \
    namespace {                 \
    [[maybe_unused]] const bool pbo_mod_registered = ModApi::init_static(id, init); \
    }
#else
#define PBO_MOD_ENTRY(id, init)                                                             \
    extern "C" __declspec(dllexport) const char pbo_mod_engine[] = PBO_MOD_ENGINE_ID;      \
    extern "C" __declspec(dllexport) void pbo_mod_init(ModApi::Context &context) { init(context); }
#endif
