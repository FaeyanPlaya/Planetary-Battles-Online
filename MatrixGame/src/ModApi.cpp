// MatrixGame - licensed under GPLv2 or any later version.
#include "ModApi.hpp"
#include "Mods.hpp"
#include "KeyBindings.hpp"

#include "stupid_logger.hpp"
#include "utils.hpp"

#include <windows.h>

#include <cstring>
#include <exception>
#include <map>
#include <utility>
#include <vector>

namespace ModApi {
namespace {
std::vector<std::pair<std::string, KeyHandler>> g_key_handlers;  // by the mod's id
}

bool Context::on() const { return Mods::on(m_Id); }

void Context::log(std::string_view text) const { lgr.notice("Mod {}: {}")(m_Id, std::string(text)); }

int Context::key(std::string_view id) const { return KeyBindings::mod_key(m_Id + "." + std::string(id)); }

std::wstring Context::setting(std::string_view id) const {
    const Mods::Mod *mod = Mods::find(Mods::installed(), m_Id);
    const Mods::SettingLine *line = mod ? Mods::find_setting(*mod, id) : nullptr;
    return line ? Mods::setting_value(*mod, *line, Mods::setting_values()) : std::wstring();
}

void Context::on_key(KeyHandler handler) const {
    if (handler) g_key_handlers.emplace_back(m_Id, handler);
}

bool key_down(int vk) {
    for (const auto &[id, handler] : g_key_handlers)
        if (Mods::on(id) && handler(vk)) return true;
    return false;
}

void clear_handlers() { g_key_handlers.clear(); }

bool init_static(const char *id, void (*init)(Context &)) {
    Context context(id, std::filesystem::path("mods") / id);
    init(context);
    return true;
}

}  // namespace ModApi

namespace Mods {
namespace {

// A plain file name of the mod's folder, a .dll: never a path out of it.
bool dll_name(const std::wstring &name) {
    if (name.empty() || name.find_first_of(L"\\/:") != std::wstring::npos || name == L"." || name == L"..") return false;
    return name.size() > 4 && _wcsicmp(name.c_str() + name.size() - 4, L".dll") == 0;
}

}  // namespace

void load_code(std::vector<Mod> &mods) {
    static std::map<std::filesystem::path, Problem> loaded;  // once a process: a mod's init runs once
    for (Mod &mod : mods) {
        if (mod.folder.empty() || mod.dll.empty() || mod.problem != Problem::None) continue;
        const auto path = std::filesystem::absolute(mod.folder / mod.dll);
        if (const auto done = loaded.find(path); done != loaded.end()) {  // as it went the first time
            mod.problem = done->second;
            continue;
        }
        if (!dll_name(mod.dll) || !std::filesystem::is_regular_file(path)) {
            mod.problem = Problem::NoDll;
            lgr.warning("Mod {}: no {} in its folder")(mod.id, utils::from_wstring(mod.dll));
            continue;
        }
        // A file that is no DLL (or another processor's) is refused quietly: Windows would ask with its "Bad Image" box.
        DWORD mode = 0;
        SetThreadErrorMode(SEM_FAILCRITICALERRORS, &mode);
        HMODULE module = LoadLibraryW(path.c_str());
        const DWORD error = GetLastError();
        SetThreadErrorMode(mode, nullptr);
        if (!module) {
            mod.problem = Problem::NotLoaded;
            lgr.warning("Mod {}: {} cannot be loaded (Win32 error {})")(mod.id, utils::from_wstring(mod.dll), error);
            continue;
        }
        const auto *engine = reinterpret_cast<const char *>(GetProcAddress(module, "pbo_mod_engine"));
        const auto init = reinterpret_cast<void (*)(ModApi::Context &)>(
                reinterpret_cast<void *>(GetProcAddress(module, "pbo_mod_init")));
        if (!engine || std::strncmp(engine, PBO_ENGINE_ID, sizeof(PBO_ENGINE_ID)) != 0) {
            mod.problem = Problem::OtherEngine;
            lgr.warning("Mod {}: built for another version of the game ({}, this one {})")(
                    mod.id, engine ? std::string(engine, strnlen(engine, 32)) : std::string("none"), PBO_ENGINE_ID);
            FreeLibrary(module);
            continue;
        }
        if (!init) {
            mod.problem = Problem::NotLoaded;
            lgr.warning("Mod {}: {} has no pbo_mod_init")(mod.id, utils::from_wstring(mod.dll));
            FreeLibrary(module);
            continue;
        }
        // Kept loaded for the rest of the game: its code may be called any time.
        ModApi::Context context(mod.id, mod.folder);
        Problem &outcome = loaded[path];
        try {
            init(context);
            lgr.notice("Mod {}: {} loaded")(mod.id, utils::from_wstring(mod.dll));
        }
        catch (const std::exception &error) {
            mod.problem = outcome = Problem::InitFailed;
            lgr.error("Mod {}: its init failed: {}")(mod.id, error.what());
        }
        catch (...) {
            mod.problem = outcome = Problem::InitFailed;
            lgr.error("Mod {}: its init failed")(mod.id);
        }
    }
}

}  // namespace Mods
