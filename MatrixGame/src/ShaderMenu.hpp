// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "ShaderPacks.hpp"

#include <algorithm>
#include <string>
#include <vector>

// The settings' Shaders screens, as a Minecraft shaders menu: the list (Shaders on or off, the packs, a pack clicked
// is the one chosen) and a pack's settings screens (its Screen= rows: settings stepped with < >, further screens, notes
// with their "i"), each with its buttons under it that do not scroll. What a screen's Done, Apply or a pack's
// settings apply is saved and drawn at once (the window saves applied() when it changes); Cancel leaves it.
// Device-independent: the state, the texts and the geometry (SettingsModel draws them in its window).
namespace ShaderMenu {

// Within the settings window, wider on the shaders screens than its other pages (SettingsModel::Layout's 460); the
// rows' counts and what is under them as with no extra rows (Menu::set_extra_rows makes the window taller on a taller
// screen).
struct Layout {
    static constexpr int width = 600;         // the window's (640x480 the least screen)
    static constexpr int row_height = 17;
    static constexpr int on_y = 58;           // the list's Shaders row
    static constexpr int list_y = on_y + 2 * row_height;  // the packs, after an empty row
    static constexpr int list_rows = 13;      // then an empty row before the legend
    // A settings screen: on top Back (a further screen's), its title in two lines, Reset; then its rows with as much room
    // over them as under them, where the chosen row's name is said in two lines; then the buttons.
    static constexpr int header_y = 24;       // the title's first line (the second a row lower)
    static constexpr int header_button_y = 32;  // Back and Reset, as wide as their texts (Menu::set_header_widths)
    static constexpr int header_button_pad = 10;  // their text from their sides
    static constexpr int settings_y = 68;     // a settings screen's rows
    static constexpr int settings_rows = 14;
    static constexpr int row_gap = 8;         // over the rows and under them
    static constexpr int legend_y = 333;      // under the rows: a problem, the author
    static constexpr int buttons_y = 356;     // two rows of buttons
    static constexpr int button_height = 20;
    static constexpr int button_step = 26;    // from a row of buttons to the next
    static constexpr int left = 40, right = width - 40;  // the packs' frame, the settings' rows, Back and Reset
    static constexpr int buttons_left = 20, buttons_right = width - 20;
    // A setting's row: its label from left, its "i", "<", its value, ">" (as SettingsModel::Layout's, wider).
    static constexpr int cell_right = right;
    static constexpr int next_x = cell_right - 16;
    static constexpr int value_width = 200;
    static constexpr int value_x = next_x - 4 - value_width;
    static constexpr int previous_x = value_x - 14;
    static constexpr int gap = 8;
    static constexpr int switch_gap = 8;      // the list's Shaders word to its switch
    static constexpr int info_x = previous_x - 20;  // a row's "i": left of its value's "<"
    static constexpr int info_size = 16;
};

enum class Button { None, Folder, PackSettings, Done, Cancel, List, Apply, Back, Reset };

struct ButtonPlace {
    Button button;
    int x, y, width;
};

struct Hit {
    enum class Kind { None, On, Pack, Row, Info, Button };
    Kind kind = Kind::None;
    int index = -1;      // the pack (Pack), the screen's row (Row, Info)
    int direction = 0;   // a setting's value cell: -1 its left half, +1 its right
    Button button = Button::None;
};

// What the window does after a button or Esc.
enum class After { Stay, MainPage };

class Menu {
public:
    Menu() = default;
    Menu(std::vector<ShaderPacks::Pack> packs, ShaderPacks::Choice applied, bool russian);

    // Saved and drawn: the window saves it (CFG\shaders.txt) when it is not ShaderPacks::choice().
    const ShaderPacks::Choice &applied() const { return m_Applied; }
    const std::vector<ShaderPacks::Pack> &packs() const { return m_Packs; }
    // The packs read anew (the folder opened): what is chosen kept by the packs' names.
    void set_packs(std::vector<ShaderPacks::Pack> packs);
    bool russian() const { return m_Russian; }

    // The list opened from the settings: as applied (the pack on chosen; none while shaders are off).
    void open();
    bool settings() const { return !m_Stack.empty(); }

    // The list: on or off as the person has it now (applied with Done), the pack chosen, the keys' row.
    bool on() const { return m_Pending.on; }
    int chosen() const { return m_Chosen; }
    int focus() const { return m_Focus; }  // -1: the Shaders row; else a pack
    int list_top() const { return m_ListTop; }
    // Shaders on or off (not applied: Apply or Done do): off, no pack chosen; on, the one chosen before since the list
    // opened, else the first.
    void toggle();
    // A pack clicked or reached with the arrows: chosen, shaders on.
    void choose(int pack);

    // A settings screen: its pack and its rows.
    const ShaderPacks::Pack *pack() const;  // the chosen one (its settings shown)
    const ShaderPacks::Screen *screen() const;
    int row_count() const;
    ShaderPacks::Item item(int row) const;
    int row_focus() const { return m_RowFocus; }
    int row_top() const { return m_RowTop; }
    bool focusable(int row) const;  // not an empty one
    bool focus_row(int row);        // the pointer over it; true when it changed

    // Rows more than Layout's (a taller window): the list, the screens, what is under them lower by as many.
    void set_extra_rows(int rows);
    int extra_rows() const { return m_Extra; }
    int list_rows() const { return Layout::list_rows + m_Extra; }
    int legend_y() const { return Layout::legend_y + m_Extra * Layout::row_height; }
    int buttons_y() const { return Layout::buttons_y + m_Extra * Layout::row_height; }
    // Under a settings screen's rows: the chosen row's name in two lines.
    int name_y() const { return Layout::settings_y + shown() * Layout::row_height + Layout::row_gap; }
    // Back's and Reset's widths: their texts' and Layout::header_button_pad each side (the window measures them).
    void set_header_widths(int back, int reset);
    // The title's place between Back and Reset (as far from either side: in the middle).
    int title_left() const { return Layout::left + std::max(m_BackWidth, m_ResetWidth) + Layout::gap; }
    int title_width() const { return Layout::width - 2 * title_left(); }
    // Every setting of the pack as its author set it (not applied: Apply or Done do); the window asks first.
    void reset();
    // A further screen (opened from a row): Back over its rows.
    bool further() const { return m_Stack.size() > 1; }
    // The screen's buttons: under the rows (and a further screen's Back).
    std::vector<ButtonPlace> buttons() const;

    // Shown from top() on; the arrows right of the list while it scrolls further that way.
    int top() const { return settings() ? m_RowTop : m_ListTop; }
    int shown() const;
    int count() const { return settings() ? row_count() : int(m_Packs.size()); }
    bool more_above() const { return top() > 0; }
    bool more_below() const { return top() + shown() < count(); }

    // Up and down: the list's Shaders row and packs (a pack reached is chosen; the buttons only with the pointer),
    // a screen's rows. The wheel scrolls without moving them.
    void move(int delta);
    void scroll(int rows);
    // A setting's value one way (true: changed); a screen's row opens it.
    bool step(int row, int direction);
    // Whether a setting row can go that way (-1, 1): not at a range's end.
    bool can_step(int row, int direction) const;
    bool open_row(int row);
    // Their keys: F, S, L, A, Backspace, Enter, Esc (the labels say them; Right on a pack: S, Enter on a screen's row: opens it).
    bool enabled(Button button) const;
    static Button button_of_key(int vk);
    After press(Button button);  // Folder: nothing here (the window opens the folder)
    After escape();              // a further screen: back to the one before (Back); else Cancel
    After enter();               // a screen's row opens it; else Done
    bool set_hover(Button button);  // the button under the pointer (drawn lit); true when it changed
    Button hover() const { return m_Hover; }

    Hit hit(int x, int y) const;
    int row_y(int place) const;  // a shown row's top by its place
    int rows_y() const;          // the first shown row's

    std::wstring title() const;
    std::wstring pack_name(int pack) const;
    std::wstring label(int row) const;
    std::wstring value(int row) const;
    std::wstring comment(int row) const;  // what a row's "i" says (empty: no "i")
    std::wstring button_text(Button button) const;
    // The line under the rows: what is wrong with the pack chosen (problem: the drawing's, for the pack applied),
    // else its author. error: it is a problem.
    std::wstring legend(const std::wstring &problem, bool &error) const;

private:
    std::vector<ShaderPacks::Pack> m_Packs;
    ShaderPacks::Choice m_Applied, m_Pending;
    bool m_Russian = false;
    int m_Chosen = -1, m_Focus = -1, m_ListTop = 0;
    int m_Before = -1;  // the pack chosen before shaders were switched off (since the list opened)
    std::vector<int> m_Stack;  // the screens opened (pack()->screens), the shown last
    std::vector<int> m_Focuses;  // the row focused on each screen before the next was opened
    int m_RowFocus = 0, m_RowTop = 0;
    Button m_Hover = Button::None;
    int m_Extra = 0;
    int m_BackWidth = 90, m_ResetWidth = 110;

    const wchar_t *text(const wchar_t *english, const wchar_t *russian) const { return m_Russian ? russian : english; }
    int find(const std::wstring &id) const;
    void apply() { m_Applied = m_Pending; }
    void show_focus();
    void open_screen(int screen);
    void first_focus();
};

}  // namespace ShaderMenu
