// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "ResourcePacks.hpp"

#include <string>
#include <vector>

// The settings' resource packs screen, as Minecraft's: the packs available on the left, those chosen on the right (the
// first over the others); a pack's picture clicked (or the pack clicked twice, or Enable / Disable, A) moves it
// across, ↑ and ↓ (PgUp, PgDn) move a chosen one higher or lower; under them the packs folder, Cancel and Done. Done
// saves the choice
// (CFG\resourcepacks.txt), used from the next start. Device-independent: the state, the texts and the geometry
// (SettingsModel draws them in its window).
namespace PackMenu {

struct Layout {
    static constexpr int width = 600;  // as the shaders screens
    static constexpr int header_y = 50;      // "Available", "Chosen"
    static constexpr int list_y = 72;
    static constexpr int row_height = 36;    // a pack: its picture, its name, a line of its description
    static constexpr int list_rows = 7;
    static constexpr int icon = 32;
    static constexpr int left_x = 20, column_width = 272, gap = 16;
    static constexpr int right_x = left_x + column_width + gap;
    static constexpr int text_x = icon + 8;  // from a row's left
    static constexpr int legend_y = 326;     // two lines: the pack in focus, then a change's restart
    static constexpr int buttons_y = 356;
    static constexpr int button_height = 20;
    static constexpr int button_step = 26;
    static constexpr int buttons_left = 20, buttons_right = width - 20, button_gap = 8;
    static constexpr int small_row_height = 17;  // a settings row's: a taller window's extra rows count in them
};

enum class Side { Available, Chosen };
enum class Button { None, Up, Turn, Down, Folder, Cancel, Done };  // Turn: Enable or Disable the pack in focus
enum class After { Stay, MainPage };

struct ButtonPlace {
    Button button;
    int x, y, width;
};

struct Hit {
    enum class Kind { None, Pack, Icon, Button };
    Kind kind = Kind::None;
    Side side = Side::Available;
    int place = -1;  // in its list
    Button button = Button::None;
};

class Menu {
public:
    Menu() = default;
    Menu(std::vector<ResourcePacks::Pack> packs, ResourcePacks::Choice applied, ResourcePacks::Choice started,
         bool russian);

    bool russian() const { return m_Russian; }
    const std::vector<ResourcePacks::Pack> &packs() const { return m_Packs; }
    // Saved by the window when Done changed it (from the next start).
    const ResourcePacks::Choice &applied() const { return m_Applied; }
    const ResourcePacks::Choice &pending() const { return m_Pending; }
    // The packs read anew (the folder opened): the choice kept by the packs' names.
    void set_packs(std::vector<ResourcePacks::Pack> packs);
    // Opened from the settings: as applied, the focus on the first chosen (else the first available).
    void open();

    // The packs of a side by their places (indices into packs()): the available by name, the chosen in their order.
    std::vector<int> list(Side side) const;
    Side focus_side() const { return m_Side; }
    int focus() const { return m_Focus; }  // the place in focus_side()'s list; -1: none there
    int focused_pack() const;              // its index into packs(); -1: none
    int top(Side side) const { return side == Side::Available ? m_Top[0] : m_Top[1]; }
    int rows() const { return Layout::list_rows + m_Extra * Layout::small_row_height / Layout::row_height; }
    bool more_above(Side side) const { return top(side) > 0; }
    bool more_below(Side side) const { return top(side) + rows() < int(list(side).size()); }

    // Up and down in a list; the other list (Left, Right: its first shown, or as near the same height as there is).
    void move(int delta);
    void switch_side(Side side);
    void focus_on(Side side, int place);
    void scroll(Side side, int rows);
    // The pack in focus across (available: on, on top of the chosen; chosen: off), the focus going with it.
    bool turn();
    // The chosen pack in focus higher (-1) or lower (+1).
    bool raise(int delta);

    void set_extra_rows(int rows);
    int extra_rows() const { return m_Extra; }
    int legend_y() const { return Layout::legend_y + m_Extra * Layout::small_row_height; }
    int buttons_y() const { return Layout::buttons_y + m_Extra * Layout::small_row_height; }
    int row_y(int place) const { return Layout::list_y + place * Layout::row_height; }
    int column_x(Side side) const { return side == Side::Available ? Layout::left_x : Layout::right_x; }

    std::vector<ButtonPlace> buttons() const;
    bool enabled(Button button) const;
    static Button button_of_key(int vk);  // F, A, PgUp, PgDn
    After press(Button button);  // Folder: nothing here (the window opens the folder)
    After escape() { return press(Button::Cancel); }
    After enter() { return press(Button::Done); }
    bool set_hover(Button button);
    Button hover() const { return m_Hover; }
    // The pointer over a pack's picture: its arrow shown (which way it would go).
    bool set_icon_hover(Side side, int place);
    bool icon_hover(Side side, int place) const { return m_IconHover && m_HoverSide == side && m_HoverPlace == place; }

    Hit hit(int x, int y) const;

    std::wstring title() const;
    std::wstring header(Side side) const;
    std::wstring name(int pack) const;
    std::wstring description(int pack) const;
    std::wstring button_text(Button button) const;  // ↑ and ↓'s without the arrow (the window draws it apart)
    // Under the lists: what is wrong with the pack in focus, else its author (the window says its description there
    // when its row cuts it); under that, notice() that a change needs a restart.
    std::wstring legend(bool &error) const;
    std::wstring notice() const;
    // The packs that would be used (chosen, installed, not broken) are not those the game started with.
    bool restart_needed() const;

private:
    std::vector<ResourcePacks::Pack> m_Packs;
    ResourcePacks::Choice m_Applied, m_Pending, m_Started;
    bool m_Russian = false;
    Side m_Side = Side::Available;
    int m_Focus = -1;
    int m_Top[2] = {0, 0};
    int m_Extra = 0;
    Button m_Hover = Button::None;
    bool m_IconHover = false;
    Side m_HoverSide = Side::Available;
    int m_HoverPlace = -1;

    const wchar_t *text(const wchar_t *english, const wchar_t *russian) const { return m_Russian ? russian : english; }
    int find(const std::wstring &id) const;
    void show_focus();
    void fix_focus();
};

}  // namespace PackMenu
