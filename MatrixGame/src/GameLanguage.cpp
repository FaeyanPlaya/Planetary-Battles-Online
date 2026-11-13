// MatrixGame - licensed under GPLv2 or any later version.
#include "GameLanguage.hpp"

#include "CBlockPar.hpp"

#include <windows.h>

#include <optional>

namespace fs = std::filesystem;

namespace GameLanguage {

Language resolve(Choice choice, unsigned short windows_language) {
    if (choice == Choice::Russian) return Language::Russian;
    if (choice == Choice::English) return Language::English;
    return PRIMARYLANGID(windows_language) == LANG_RUSSIAN ? Language::Russian : Language::English;
}

Language windows() { return resolve(Choice::System, GetUserDefaultUILanguage()); }

namespace {
std::optional<Language> g_current;
}

void start(Choice choice) { g_current = resolve(choice, GetUserDefaultUILanguage()); }
void start(Language language) { g_current = language; }
bool started() { return g_current.has_value(); }

Language current() { return g_current ? *g_current : windows(); }

bool russian() { return current() == Language::Russian; }

const wchar_t *folder(Language language) { return language == Language::Russian ? L"Rus" : L"Eng"; }

namespace {

Language other(Language language) { return language == Language::Russian ? Language::English : Language::Russian; }

bool present(const fs::path &file) {
    std::error_code error;
    return fs::is_regular_file(file, error);
}

}  // namespace

fs::path names_file(const fs::path &game, Language language) {
    const fs::path cfg = game / L"CFG";
    for (const fs::path &file : {cfg / folder(language) / L"Lang.dat", cfg / L"Lang.dat", cfg / folder(other(language)) / L"Lang.dat"})
        if (present(file)) return file;
    return cfg / L"Lang.dat";
}

fs::path voices_file(const fs::path &game, Language language) {
    const fs::path data = game / L"DATA";
    for (const fs::path &file : {data / (std::wstring(L"voices") + folder(language) + L".pkg"), data / L"voices.pkg",
                                 data / (std::wstring(L"voices") + folder(other(language)) + L".pkg")})
        if (present(file)) return file;
    return data / L"voices.pkg";
}

fs::path texts_file(const fs::path &game, Language language) {
    const fs::path file = game / L"CFG" / folder(language) / L"robots.dat";
    return present(file) ? file : fs::path();
}

void overlay_texts(Base::CBlockPar &data, Base::CBlockPar &texts) {
    for (const wchar_t *name : {L"AllLabels", L"Templates"}) {
        Base::CBlockPar *from = texts.BlockGetNE(name);
        Base::CBlockPar *to = data.BlockGetNE(name);
        if (from && to) to->CopyFrom(*from);
    }
}

}  // namespace GameLanguage
