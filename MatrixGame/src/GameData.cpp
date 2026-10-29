// MatrixGame - licensed under GPLv2 or any later version.
#include "GameData.hpp"
#include "ExecutablePath.hpp"

#include <windows.h>
#include <shlobj.h>

#include <stupid_logger.hpp>

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <format>
#include <fstream>
#include <optional>

namespace fs = std::filesystem;

namespace GameData {
namespace {

std::wstring folded(std::wstring text) {
    for (auto &ch : text) ch = wchar_t(std::towlower(ch));
    return text;
}

bool file_exists(const fs::path &path) {
    std::error_code error;
    return fs::is_regular_file(path, error);
}

bool directory(const fs::path &path) {
    std::error_code error;
    return fs::is_directory(path, error);
}

std::optional<std::wstring> registry_string(HKEY root, const wchar_t *key, const wchar_t *value) {
    wchar_t buffer[1024];
    DWORD size = sizeof(buffer);
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, buffer, &size) != ERROR_SUCCESS)
        return std::nullopt;
    return std::wstring(buffer);
}

// Values of every subkey of root\key: value_name where match_name (if given) mentions Rangers.
std::vector<fs::path> registry_folders(HKEY root, const wchar_t *key, const wchar_t *value_name, const wchar_t *match_name) {
    std::vector<fs::path> out;
    HKEY parent;
    if (RegOpenKeyExW(root, key, 0, KEY_READ, &parent) != ERROR_SUCCESS) return out;
    wchar_t name[256];
    for (DWORD i = 0;; ++i) {
        DWORD length = 256;
        if (RegEnumKeyExW(parent, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        const std::wstring sub = std::wstring(key) + L"\\" + name;
        if (match_name) {
            const auto title = registry_string(root, sub.c_str(), match_name);
            if (!title || folded(*title).find(L"rangers") == std::wstring::npos) continue;
        }
        if (const auto folder = registry_string(root, sub.c_str(), value_name); folder && !folder->empty())
            out.emplace_back(*folder);
    }
    RegCloseKey(parent);
    return out;
}

std::vector<fs::path> fixed_drives() {
    std::vector<fs::path> drives;
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        const std::wstring root = std::wstring(1, wchar_t(L'A' + i)) + L":\\";
        if (GetDriveTypeW(root.c_str()) == DRIVE_FIXED) drives.emplace_back(root);
    }
    return drives;
}

std::string read_text(const fs::path &file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

bool russian_ui() { return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN; }

const wchar_t *text(bool ru, const wchar_t *english, const wchar_t *russian) { return ru ? russian : english; }

constexpr const wchar_t *title = L"Planetary Battles Online";

std::optional<fs::path> browse_folder(bool ru) {
    BROWSEINFOW info{};
    info.lpszTitle = text(ru, L"The folder of Space Rangers HD (with Rangers.exe):",
                          L"\u041f\u0430\u043f\u043a\u0430 Space Rangers HD (\u0432 \u043d\u0435\u0439 \u043b\u0435\u0436\u0438\u0442 Rangers.exe):");
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LPITEMIDLIST item = SHBrowseForFolderW(&info);
    std::optional<fs::path> result;
    if (item) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(item, path)) result = fs::path(path);
        CoTaskMemFree(item);
    }
    return result;
}

bool copy_files(const fs::path &install, const fs::path &game, bool ru) {
    for (const Copy &c : files_to_copy(install, game, ru)) {
        std::error_code error;
        fs::create_directories(c.to.parent_path(), error);
        if (file_exists(c.to)) continue;  // the person's own file stays
        if (!CopyFileW(c.from.c_str(), c.to.c_str(), TRUE)) {
            lgr.error("Setup: cannot copy {} (Windows error {})")(c.from.string(), GetLastError());
            if (c.required) return false;
            continue;
        }
        lgr.notice("Setup: copied {}")(c.from.string());
    }
    if (!file_exists(game / L"DATA" / L"robots.pkg")) return false;
    std::error_code error;
    save_install(game, {install, fs::is_directory(install / L"Mods", error), false});
    return true;
}

fs::path install_file(const fs::path &game) { return game / L"CFG" / L"game-path.txt"; }

}  // namespace

bool is_install(const fs::path &folder) {
    return file_exists(folder / L"Rangers.exe") && file_exists(folder / L"DATA" / L"robots.pkg");
}

std::vector<fs::path> steam_libraries(std::string_view vdf) {
    std::vector<fs::path> out;
    // Lines like:  "path"		"D:\\SteamLibrary"
    std::size_t at = 0;
    while ((at = vdf.find("\"path\"", at)) != std::string_view::npos) {
        at += 6;
        const auto open = vdf.find('"', at);
        if (open == std::string_view::npos) break;
        const auto close = vdf.find('"', open + 1);
        if (close == std::string_view::npos) break;
        std::string value;
        for (std::size_t i = open + 1; i < close; ++i) {
            if (vdf[i] == '\\' && i + 1 < close && vdf[i + 1] == '\\') ++i;  // escaped backslash
            value += vdf[i];
        }
        int length = MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0);
        std::wstring wide(std::size_t(std::max(0, length)), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), wide.data(), length);
        if (!wide.empty()) out.emplace_back(wide);
        at = close + 1;
    }
    return out;
}

std::vector<fs::path> installs_in(const std::vector<fs::path> &folders) {
    std::vector<fs::path> out;
    std::vector<std::wstring> seen;
    const auto add = [&](const fs::path &folder) {
        if (!is_install(folder)) return;
        std::error_code error;
        auto canonical = fs::weakly_canonical(folder, error);
        const auto key = folded((error ? folder : canonical).wstring());
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) return;
        seen.push_back(key);
        out.push_back(error ? folder : canonical);
    };
    const auto mentions_rangers = [](const fs::path &p) {
        return folded(p.filename().wstring()).find(L"rangers") != std::wstring::npos;
    };
    for (const fs::path &folder : folders) {
        if (!directory(folder)) continue;
        add(folder);
        std::error_code error;
        for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
            if (!it->is_directory(error)) continue;
            if (mentions_rangers(it->path())) add(it->path());
            // "Games\Space Rangers\HD", "SR HD\Space Rangers HD A War Apart" and the like.
            std::error_code inner_error;
            for (fs::directory_iterator in(it->path(), inner_error), in_end; !inner_error && in != in_end;
                 in.increment(inner_error)) {
                if (in->is_directory(inner_error) && (mentions_rangers(in->path()) || mentions_rangers(it->path())))
                    add(in->path());
            }
        }
    }
    return out;
}

std::vector<fs::path> find_installs() {
    std::vector<fs::path> folders;
    // Steam: its own folder and every library it knows, on any drive.
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        for (const wchar_t *key : {L"Software\\Valve\\Steam", L"SOFTWARE\\WOW6432Node\\Valve\\Steam"}) {
            for (const wchar_t *value : {L"SteamPath", L"InstallPath"}) {
                const auto steam = registry_string(root, key, value);
                if (!steam) continue;
                const fs::path base(*steam);
                folders.push_back(base / L"steamapps" / L"common");
                for (const fs::path &library : steam_libraries(read_text(base / L"steamapps" / L"libraryfolders.vdf")))
                    folders.push_back(library / L"steamapps" / L"common");
            }
        }
    }
    // GOG and other installers.
    for (const fs::path &p : registry_folders(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\GOG.com\\Games", L"path", L"gameName"))
        folders.push_back(p);
    for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER})
        for (const wchar_t *key : {L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
                                   L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"})
            for (const fs::path &p : registry_folders(root, key, L"InstallLocation", L"DisplayName")) folders.push_back(p);
    // The usual places on every fixed drive.
    for (const fs::path &drive : fixed_drives()) {
        for (const wchar_t *sub : {L"", L"SteamLibrary\\steamapps\\common", L"Steam\\steamapps\\common",
                                   L"Program Files (x86)\\Steam\\steamapps\\common", L"Program Files\\Steam\\steamapps\\common",
                                   L"Games", L"Games\\SteamLibrary\\steamapps\\common", L"GOG Games", L"GOG Galaxy\\Games",
                                   L"Program Files (x86)\\GOG Galaxy\\Games", L"Program Files (x86)", L"Program Files",
                                   L"\u0418\u0433\u0440\u044b"})
            folders.push_back(drive / sub);
    }
    return installs_in(folders);
}

std::vector<Copy> files_to_copy(const fs::path &install, const fs::path &game, bool russian) {
    std::vector<Copy> out;
    const fs::path data = install / L"DATA";
    out.push_back({data / L"robots.pkg", game / L"DATA" / L"robots.pkg", true});
    if (file_exists(data / L"Sound.pkg")) out.push_back({data / L"Sound.pkg", game / L"DATA" / L"sound.pkg"});
    const wchar_t *voices[] = {russian ? L"voicesRus.pkg" : L"voicesEng.pkg", russian ? L"voicesEng.pkg" : L"voicesRus.pkg"};
    for (const wchar_t *name : voices)
        if (file_exists(data / name)) {
            out.push_back({data / name, game / L"DATA" / L"voices.pkg"});
            break;
        }
    // Map names and descriptions for the map windows.
    const wchar_t *languages[] = {russian ? L"Rus" : L"Eng", russian ? L"Eng" : L"Rus"};
    for (const wchar_t *language : languages)
        if (file_exists(install / L"CFG" / language / L"Lang.dat")) {
            out.push_back({install / L"CFG" / language / L"Lang.dat", game / L"CFG" / L"Lang.dat"});
            break;
        }
    // The robots' configuration (a game file, not shipped): the Russian one first whatever the interface, so that
    // every person's data fingerprint matches.
    for (const wchar_t *language : {L"Rus", L"Eng"})
        if (file_exists(install / L"CFG" / language / L"robots.dat")) {
            out.push_back({install / L"CFG" / language / L"robots.dat", game / L"CFG" / L"robots.dat"});
            break;
        }
    return out;
}

bool setup(const fs::path &game, bool assume_yes) {
    // Tests of a deployment without resources must not find the copy of the game on their PC.
    if (_wgetenv(L"PBO_NO_SETUP")) return false;
    const bool ru = russian_ui();
    const auto installs = find_installs();
    lgr.notice("Setup: {} Space Rangers HD installs found")(installs.size());
    for (const fs::path &install : installs) {
        lgr.notice("Setup: found {}")(install.string());
        int answer = IDYES;
        if (!assume_yes) {
            const std::wstring message =
                std::wstring(text(ru, L"Space Rangers HD was found in\n", L"Space Rangers HD \u043d\u0430\u0439\u0434\u0435\u043d\u0430 \u0432 \u043f\u0430\u043f\u043a\u0435\n")) + install.wstring() +
                text(ru, L"\n\nCopy the game files from it (about 290 MB, a few seconds) into\n",
                     L"\n\n\u0421\u043a\u043e\u043f\u0438\u0440\u043e\u0432\u0430\u0442\u044c \u0438\u0437 \u043d\u0435\u0451 \u0444\u0430\u0439\u043b\u044b \u0438\u0433\u0440\u044b (\u043e\u043a\u043e\u043b\u043e 290 \u041c\u0411, \u043d\u0435\u0441\u043a\u043e\u043b\u044c\u043a\u043e \u0441\u0435\u043a\u0443\u043d\u0434) \u0432 \u043f\u0430\u043f\u043a\u0443\n") +
                game.wstring() +
                text(ru, L"?\n\nThe installed game is not changed.\nNo: look further. Cancel: quit.",
                     L"?\n\n\u0423\u0441\u0442\u0430\u043d\u043e\u0432\u043b\u0435\u043d\u043d\u0430\u044f \u0438\u0433\u0440\u0430 \u043d\u0435 \u0438\u0437\u043c\u0435\u043d\u044f\u0435\u0442\u0441\u044f.\n\u041d\u0435\u0442: \u0438\u0441\u043a\u0430\u0442\u044c \u0434\u0430\u043b\u044c\u0448\u0435. \u041e\u0442\u043c\u0435\u043d\u0430: \u0432\u044b\u0439\u0442\u0438.");
            answer = MessageBoxW(nullptr, message.c_str(), title, MB_YESNOCANCEL | MB_ICONQUESTION);
        }
        if (answer == IDCANCEL) return false;
        if (answer != IDYES) continue;
        if (copy_files(install, game, ru)) return true;
        MessageBoxW(nullptr, text(ru, L"The files could not be copied; see test.log.", L"\u041d\u0435 \u0443\u0434\u0430\u043b\u043e\u0441\u044c \u0441\u043a\u043e\u043f\u0438\u0440\u043e\u0432\u0430\u0442\u044c \u0444\u0430\u0439\u043b\u044b, \u043f\u043e\u0434\u0440\u043e\u0431\u043d\u043e\u0441\u0442\u0438 \u0432 test.log."),
                    title, MB_OK | MB_ICONWARNING);
        return false;
    }
    if (assume_yes) return false;
    const wchar_t *missing = installs.empty()
        ? text(ru, L"Space Rangers HD was not found on this PC (Steam libraries, GOG, the usual game folders on every drive).\n\nShow the folder of the game?",
               L"Space Rangers HD \u043d\u0435 \u043d\u0430\u0439\u0434\u0435\u043d\u0430 \u043d\u0430 \u044d\u0442\u043e\u043c \u041f\u041a (\u0431\u0438\u0431\u043b\u0438\u043e\u0442\u0435\u043a\u0438 Steam, GOG, \u043e\u0431\u044b\u0447\u043d\u044b\u0435 \u043f\u0430\u043f\u043a\u0438 \u0438\u0433\u0440 \u043d\u0430 \u0432\u0441\u0435\u0445 \u0434\u0438\u0441\u043a\u0430\u0445).\n\n\u0423\u043a\u0430\u0437\u0430\u0442\u044c \u043f\u0430\u043f\u043a\u0443 \u0441 \u0438\u0433\u0440\u043e\u0439 \u0432\u0440\u0443\u0447\u043d\u0443\u044e?")
        : text(ru, L"Show another folder of the game?", L"\u0423\u043a\u0430\u0437\u0430\u0442\u044c \u0434\u0440\u0443\u0433\u0443\u044e \u043f\u0430\u043f\u043a\u0443 \u0441 \u0438\u0433\u0440\u043e\u0439?");
    while (MessageBoxW(nullptr, missing, title, MB_YESNO | MB_ICONQUESTION) == IDYES) {
        const auto folder = browse_folder(ru);
        if (!folder) break;
        if (is_install(*folder)) return copy_files(*folder, game, ru);
        // The game folder itself or one of its parts may have been chosen.
        for (const fs::path &inside : installs_in({*folder, folder->parent_path()}))
            return copy_files(inside, game, ru);
        missing = text(ru, L"That folder has no Rangers.exe and DATA\\robots.pkg. Show another?",
                       L"\u0412 \u044d\u0442\u043e\u0439 \u043f\u0430\u043f\u043a\u0435 \u043d\u0435\u0442 Rangers.exe \u0438 DATA\\robots.pkg. \u0423\u043a\u0430\u0437\u0430\u0442\u044c \u0434\u0440\u0443\u0433\u0443\u044e?");
    }
    return false;
}

std::optional<Install> load_install(const fs::path &game) {
    std::ifstream in(install_file(game), std::ios::binary);
    if (!in) return std::nullopt;
    std::string line;
    std::getline(in, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    Install install;
    try {
        install.path = fs::path(std::u8string(line.begin(), line.end()));
    }
    catch (...) {
        return std::nullopt;
    }
    std::string word;
    while (in >> word) {
        if (word == "mods") install.mods_seen = true;
        else if (word == "never") install.never = true;
    }
    return install;
}

void save_install(const fs::path &game, const Install &install) {
    std::error_code error;
    fs::create_directories(install_file(game).parent_path(), error);
    std::ofstream out(install_file(game), std::ios::binary | std::ios::trunc);
    const std::u8string path = install.path.u8string();
    out.write(reinterpret_cast<const char *>(path.data()), std::streamsize(path.size()));
    out << "\r\n";
    if (install.mods_seen) out << "mods\r\n";
    if (install.never) out << "never\r\n";
    if (!out) lgr.warning("Cannot write {}")(install_file(game).string());
}

fs::path game_mods(const fs::path &game, bool ask) {
    if (_wgetenv(L"PBO_NO_SETUP")) return {};
    const auto has_mods = [](const fs::path &install) {
        std::error_code error;
        return !install.empty() && fs::is_directory(install / L"Mods", error);
    };
    auto saved = load_install(game);
    if (!saved) {
        // Files copied before the path was kept (or by hand): look once, without asking.
        Install found;
        for (const fs::path &install : find_installs()) {
            if (found.path.empty() || (!found.mods_seen && has_mods(install))) found = {install, has_mods(install), false};
        }
        save_install(game, found);
        lgr.notice("Space Rangers HD for map mods: {}")(found.path.empty() ? std::string("none found") : found.path.string());
        return found.mods_seen ? found.path / L"Mods" : fs::path();
    }
    if (saved->never) return {};
    if (has_mods(saved->path)) {
        if (!saved->mods_seen) save_install(game, {saved->path, true, false});
        return saved->path / L"Mods";
    }
    if (!saved->mods_seen || !ask) return {};
    const bool ru = russian_ui();
    const std::wstring message =
        std::wstring(text(ru, L"The Mods folder of Space Rangers HD (the map mods come from it) is gone from\n",
                          L"\u041f\u0430\u043f\u043a\u0430 Mods \u0438\u0433\u0440\u044b Space Rangers HD (\u0438\u0437 \u043d\u0435\u0451 \u0431\u0435\u0440\u0443\u0442\u0441\u044f \u043c\u043e\u0434\u044b \u043a\u0430\u0440\u0442) \u043f\u0440\u043e\u043f\u0430\u043b\u0430 \u0438\u0437\n")) +
        saved->path.wstring() +
        text(ru, L"\n\nYes: look for it again. No: later (this start goes without it). Cancel: never look again.",
             L"\n\n\u0414\u0430: \u043d\u0430\u0439\u0442\u0438 \u0435\u0451 \u0437\u0430\u043d\u043e\u0432\u043e. \u041d\u0435\u0442: \u043e\u0442\u043b\u043e\u0436\u0438\u0442\u044c (\u044d\u0442\u043e\u0442 \u0437\u0430\u043f\u0443\u0441\u043a \u0431\u0435\u0437 \u043d\u0435\u0451). \u041e\u0442\u043c\u0435\u043d\u0430: \u0431\u043e\u043b\u044c\u0448\u0435 \u043d\u0435 \u0438\u0441\u043a\u0430\u0442\u044c.");
    const int answer = MessageBoxW(nullptr, message.c_str(), title, MB_YESNOCANCEL | MB_ICONQUESTION);
    if (answer == IDCANCEL) {
        save_install(game, {saved->path, true, true});
        return {};
    }
    if (answer != IDYES) return {};
    for (const fs::path &install : find_installs())
        if (has_mods(install)) {
            save_install(game, {install, true, false});
            lgr.notice("Space Rangers HD for map mods found again: {}")(install.string());
            return install / L"Mods";
        }
    const wchar_t *prompt = text(ru, L"No Space Rangers HD with a Mods folder was found. Show the folder of the game?",
                                 L"\u041d\u0435 \u043d\u0430\u0448\u043b\u0438 Space Rangers HD \u0441 \u043f\u0430\u043f\u043a\u043e\u0439 Mods. \u0423\u043a\u0430\u0437\u0430\u0442\u044c \u043f\u0430\u043f\u043a\u0443 \u0438\u0433\u0440\u044b \u0432\u0440\u0443\u0447\u043d\u0443\u044e?");
    while (MessageBoxW(nullptr, prompt, title, MB_YESNO | MB_ICONQUESTION) == IDYES) {
        const auto folder = browse_folder(ru);
        if (!folder) break;
        for (const fs::path &install : {*folder, folder->parent_path()})
            if (has_mods(install) && file_exists(install / L"DATA" / L"robots.pkg")) {
                save_install(game, {install, true, false});
                return install / L"Mods";
            }
        prompt = text(ru, L"That folder is not the game with a Mods folder. Show another?",
                      L"\u0412 \u044d\u0442\u043e\u0439 \u043f\u0430\u043f\u043a\u0435 \u043d\u0435\u0442 \u0438\u0433\u0440\u044b \u0441 \u043f\u0430\u043f\u043a\u043e\u0439 Mods. \u0423\u043a\u0430\u0437\u0430\u0442\u044c \u0434\u0440\u0443\u0433\u0443\u044e?");
    }
    return {};
}

std::uint64_t hash_bytes(const void *data, std::size_t size, std::uint64_t seed) {
    const auto *p = static_cast<const unsigned char *>(data);
    std::uint64_t h = seed;
    for (std::size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

std::uint64_t hash_file(const fs::path &file, std::uint64_t seed) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return 0;
    std::vector<char> buffer(1 << 20);
    std::uint64_t h = seed;
    while (in) {
        in.read(buffer.data(), std::streamsize(buffer.size()));
        h = hash_bytes(buffer.data(), std::size_t(in.gcount()), h);
    }
    return h;
}

std::string hex(std::uint64_t value) { return std::format("{:016x}", value); }

const std::string &data_fingerprint() {
    static const std::string value = [] {
        // The configuration the game reads: robots.dat, else the text files beside it.
        std::uint64_t h = hash_file(L"DATA\\robots.pkg");
        if (file_exists(L"CFG\\robots.dat")) h = hash_file(L"CFG\\robots.dat", h);
        else {
            h = hash_file(L"CFG\\robots\\data.txt", h);
            h = hash_file(L"CFG\\robots\\iface.txt", h);
        }
        return hex(h);
    }();
    return value;
}

const std::string &build_fingerprint() {
    static const std::string value = hex(hash_file(Startup::ExecutablePath()));
    return value;
}

}  // namespace GameData
