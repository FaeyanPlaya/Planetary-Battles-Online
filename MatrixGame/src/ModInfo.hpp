// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "SettingsModel.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Mods {
struct Mod;
}

// A mod's window, as SR HD's mods menu opens one with its "i": over the lobby's or the settings' mods list, its name,
// whether it is a battle's or a person's, what it does (a line each, as its mod.txt has them), what it needs and what
// it cannot be on with, who made it and what is wrong with it on this PC; as wide and as tall as its text. Any click,
// Esc or Enter closes it. Also the question asked when a mod switched on needs others. Device-independent: the text
// and geometry (ModInfoWindow shows them).
namespace ModInfo {

using SettingsModel::Language;

struct Info {
    std::wstring name, description, author;  // the description: its lines split by \n
    std::vector<std::wstring> depends, conflicts;  // their names (an id: a mod this PC lacks)
    enum class Kind { Battle, Personal, Other } kind = Kind::Other;  // Other: not a mod (a lobby's switch, a person)
    std::wstring problem;  // what is wrong with it here, said in the window's language; empty: nothing
    bool operator==(const Info &) const = default;
};

// An installed mod's window: the names of the mods it names from `all` (the installed ones).
Info of(const Mods::Mod &mod, const std::vector<Mods::Mod> &all, Language language);

// Laid out on this grid, drawn `scale` times larger (as the lobby).
struct Layout {
    static constexpr int min_width = 420;
    static constexpr int max_width = 660;   // a longer line is broken at its spaces
    static constexpr int margin = 30;
    static constexpr int title_y = 36;      // under the frame's top
    static constexpr int kind_y = 62;
    static constexpr int first_y = 90;
    static constexpr int line_height = 18;
    static constexpr int button_gap = 16;   // under the last line, the buttons
    static constexpr int bottom = 38;       // the buttons' row and the frame under it
    static constexpr int button_width = 120;
    static constexpr int item_indent = 12;  // a "- " item's further lines
};

// The drawn width of a text in a font (the window's fonts); empty (tests): about half the font's size a letter.
using TextWidth = std::function<int(const std::wstring &, const wchar_t *font)>;

// The window's lines: (text, colour role) in order; a long text broken at its spaces to the window's width.
enum class Role { Text, Label, Problem };
struct Line {
    std::wstring text;
    Role role;
    int indent = 0;  // in the grid's units: the further lines of a "- " item, under its text
    bool operator==(const Line &) const = default;
};
std::vector<Line> lines(const Info &info, Language language, double scale, const TextWidth &width);
int width(const Info &info, Language language, double scale, const TextWidth &width);  // in the grid's units
int height(const std::vector<Line> &lines);                                            // in the grid's units

// The hint markup of the window (CMatrixHint::Build).
std::wstring hint_template(const Info &info, Language language, double scale, const TextWidth &width);

// Switching a mod on that needs others: switch those on too (yes), or leave it off (no).
struct Question {
    std::wstring mod;                 // its name
    std::vector<std::wstring> needs{};  // the names of those it needs that are off
    bool operator==(const Question &) const = default;
};
std::wstring question_template(const Question &question, Language language, double scale, const TextWidth &width);
// A point of the question's window, in the grid's units from its corner: 1 its Yes, 0 its No, -1 neither.
int question_hit(const Question &question, Language language, double scale, const TextWidth &width, int x, int y);

}  // namespace ModInfo
