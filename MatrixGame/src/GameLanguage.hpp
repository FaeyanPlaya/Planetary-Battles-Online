// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <filesystem>
#include <string>

namespace Base { class CBlockPar; }

// The language of the game's texts: this project's own (every window has them in Russian and English) and, when the
// person's Space Rangers HD has them, the game's own (its labels and hints, map names, speech). The person chooses it
// in the settings (Language=System|Russian|English in CFG\standalone.txt); System, the first start's, follows
// Windows: Russian when Windows speaks Russian, else English.
namespace GameLanguage {

enum class Language { English, Russian };
enum class Choice { System, Russian, English };

Language resolve(Choice choice, unsigned short windows_language);  // a LANGID
Language windows();  // Windows' interface language: Russian or else English

// This run's, fixed as the game starts (CFG\standalone.txt's choice: a change in the settings is from the next start);
// Windows' before that.
void start(Choice choice);
void start(Language language);  // the original host's (its language folder)
bool started();
Language current();
bool russian();

// A text in the language, else in the other one (a mod's or a shader pack's that has only one).
inline const std::wstring &pick(bool russian, const std::wstring &text_ru, const std::wstring &text_en) {
    const std::wstring &chosen = russian ? text_ru : text_en;
    return chosen.empty() ? (russian ? text_en : text_ru) : chosen;
}

const wchar_t *folder(Language language);  // Rus, Eng: the game's own folders' names (CFG\Rus, voicesRus.pkg)

// The game's files of a language, copied from the person's Space Rangers HD (GameData::language_files), within the
// game folder: the language's own, else the one of the first start (CFG\Lang.dat, DATA\voices.pkg), else the other
// language's. The first start's when there is none.
std::filesystem::path names_file(const std::filesystem::path &game, Language language);   // the maps' names: Lang.dat
std::filesystem::path voices_file(const std::filesystem::path &game, Language language);  // DATA\voices*.pkg
// CFG\<Rus|Eng>\robots.dat: the game's own texts in the language; empty when that was not copied.
std::filesystem::path texts_file(const std::filesystem::path &game, Language language);

// The game's texts of another configuration over this one's: its labels (AllLabels, with the menus' Replaces) and its
// hints' templates. The battle's data stays this one's (everyone's, GameData::data_fingerprint).
void overlay_texts(Base::CBlockPar &data, Base::CBlockPar &texts);

}  // namespace GameLanguage
