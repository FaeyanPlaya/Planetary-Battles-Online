// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "StandaloneSettings.hpp"
#include "KeyBindings.hpp"
#include "Mods.hpp"
#include "ShaderPacks.hpp"
#include "ShaderMenu.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// Device-independent state, text and geometry of the in-game settings window.
namespace ModInfo {
struct Info;
struct Question;
}

namespace SettingsModel {

using GameLanguage::Language;

// The frame of this project's windows (the hint markup's first number): the game's large panel without its help mark
// (CMatrixHint makes it from border 2).
constexpr int window_border = 5;

enum class Row {
    DisplayGroup,
    Resolution,
    WindowMode,
    FullscreenMode,
    Theme,           // the windows' look: as Windows', light, dark (UiTheme)
    Language,        // the texts' (GameLanguage): as Windows', Russian, English; from the next start
    SoundGroup,
    SoundVolume,
    MusicVolume,
    GraphicsGroup,
    StencilShadows,
    ProjectedShadows,
    Skybox,
    RobotShadows,
    ShowFps,         // the frame rate under the battle's Menu button
    Shaders,         // opens the shaders page (ShaderPacks): on, the pack, its settings
    GameplayGroup,
    CameraSpeed,
    RandomGenerator,
    DelayedDamage,
    Keys,            // opens the keys page (18, key rebinding)
    Mods,            // opens the mods page, this person's own mods (the rows after it)
    FarView,         // farview and its scale
    InterfaceFixes,  // interface (a mod of the mods folder): grey and Off while it is not there
    FirstPerson,     // firstperson
    NewSky,          // newsky: grey and Off while the game lacks PBNewSky
    SavedDesigns,    // how many designs built are kept between battles (the engine's, not a mod)
    Count
};

constexpr int row_count = static_cast<int>(Row::Count);
// The main page has the rows up to Mods, an empty row before each group but the first; the mods page the rest (and
// the rows of the mods folder's personal mods, numbered from row_count: Window). The window is as many rows high on
// every page (Window::rows_shown): the main page's all where the screen has the room, fewer on a small screen (the
// pages scroll).
constexpr int main_rows = static_cast<int>(Row::Mods) + 1;
constexpr int mods_first = static_cast<int>(Row::FarView);
constexpr int main_groups = 4;
constexpr int tall_rows = main_rows + main_groups - 1;  // the main page with its empty rows
constexpr int least_rows = 21;                           // the window on a 640x480 screen
// The main page's rows in order, -1 an empty one.
std::vector<int> main_page();

struct Resolution {
    int width;
    int height;
    bool operator==(const Resolution &) const = default;
};

// Window geometry in pixels, relative to the window's top-left corner.
struct Layout {
    static constexpr int width = 460;
    // The window's height on the least screen; the shaders screens' (with no extra rows) on every screen. The other
    // pages: Window::legend_y and Window::content_height, by the rows shown.
    static constexpr int title_y = 30;
    static constexpr int first_row_y = 55;
    static constexpr int row_height = 17;  // (15 while the mods were rows of the main page: 640x480)
    static constexpr int label_x = 70;
    static constexpr int previous_x = 250;  // "<" marker; the left half of the value cell steps back.
    static constexpr int value_x = 264;
    static constexpr int value_width = 136;
    static constexpr int next_x = 404;  // ">" marker; the right half of the value cell steps forward.
    static constexpr int cell_right = 420;
    // A mod row's switch (in the value cell's middle) and its "i" just left of it, as the lobby's: SR HD's mods menu's
    // buttons. The mods page has no arrows (a click on the value's halves steps it, as everywhere).
    static constexpr int button_size = 16;
    static constexpr int switch_x = value_x + (value_width - button_size) / 2;
    static constexpr int info_x = switch_x - 6 - button_size;
    static constexpr int legend_y = first_row_y + least_rows * row_height + 2;
    static constexpr int content_height = legend_y + row_height;  // (a failed save's text in the legend's place)
};

struct Hit {
    int row = -1;       // Option row under the point, or -1.
    int direction = 0;  // -1 or +1 when the value cell was hit, otherwise 0.
    bool info = false;  // the row's "i": its mod's window (ModInfo)
};

// The runtime bitmap names of a mod row's switch and "i" (Layout::button_size square; SettingsWindow makes them).
std::wstring switch_bitmap(bool on);
std::wstring info_bitmap();

bool is_group(int row);
bool mods_row(int row);
// A row that opens a page (Keys, Mods): no arrows, any click or Enter opens it.
bool opener(int row);
bool needs_restart(int row);
// A battle shared by several people keeps its rules: rule rows then apply from the next battle.
void lock_battle_rules(bool locked);
// The top of the window's row at a place (0 the first shown).
int row_top(int place);
// The rows a screen this high has room for (the window with its frame and buttons on it), least_rows to tall_rows.
int rows_for_screen(int screen_height);

// Adapter modes become a sorted, unique list of at least 640x480 that always contains the current size.
std::vector<Resolution> resolution_choices(std::vector<Resolution> modes, Resolution current);

// Russian when the game configuration's own menu text is Cyrillic.
Language detect_language(const std::wstring &menu_text);

class Window {
public:
    // default_personal: the personal mods of a person who never chose (PersonalMods=Default); unavailable: personal
    // mods whose files this PC lacks (their rows grey, Off and fixed; the choice kept for when they are back).
    // keys: the person's keys now (the keys page changes them; base: the game's, what Del puts back).
    // mods: the installed ones (the personal mods of the mods folder get rows on the mods page); mod_settings: the
    // person's values of their settings now.
    Window(StandaloneSettings::Values values, std::vector<Resolution> resolutions, Language language,
           std::vector<std::string> default_personal = {}, std::vector<std::string> unavailable = {},
           KeyBindings::Keys keys = KeyBindings::base(), KeyBindings::Keys base = KeyBindings::base(),
           const std::vector<Mods::Mod> &mods = {}, Mods::SettingValues mod_settings = {},
           std::vector<ShaderPacks::Pack> packs = {}, ShaderPacks::Choice shaders = {});

    const StandaloneSettings::Values &values() const { return m_Values; }
    bool changed() const { return m_Values != m_Original || m_Keys != m_KeysOriginal || mod_settings_changed(); }
    const KeyBindings::Keys &keys() const { return m_Keys; }
    bool keys_changed() const { return m_Keys != m_KeysOriginal; }
    const Mods::SettingValues &mod_settings() const { return m_ModSettings; }
    bool mod_settings_changed() const { return m_ModSettings != m_ModSettingsOriginal; }
    // The shaders as their screens applied them (their Done, Apply...: the window saves them at once; the window's own
    // OK and Cancel leave them).
    const ShaderPacks::Choice &shaders() const { return m_ShaderMenu.applied(); }
    // Why the pack applied does not draw (ShaderRender::problem): the shaders screens' legend.
    void set_shaders_problem(std::wstring problem) { m_ShadersProblem = std::move(problem); }
    const std::wstring &shaders_problem() const { return m_ShadersProblem; }
    // The drawn width of a text in a font (the window sets the game's): a text longer than its place on the shaders
    // screens is cut with "..." (the legend then says it whole). Without it (tests) nothing is cut.
    using TextWidth = std::function<int(const std::wstring &, const wchar_t *font)>;
    void set_text_width(TextWidth width) { m_TextWidth = std::move(width); }
    int width() const;  // the window's on the page shown (Layout::width; the shaders screens' wider)
    // A text cut to a place of a text element `width` wide.
    std::wstring fit(const std::wstring &text, const wchar_t *font, int width) const;
    int text_width(const std::wstring &text, const wchar_t *font) const;
    // A text in at most `count` lines of a place `width` wide, broken at spaces (the last cut with "...").
    std::vector<std::wstring> lines(const std::wstring &text, const wchar_t *font, int width, int count) const;
    // The shaders screens' window taller by these rows (as the screen allows): more packs and settings shown.
    void set_extra_rows(int rows) { m_ShaderMenu.set_extra_rows(rows); }
    // The window's content height on the page shown (the shaders screens' Layout::content_height and their extra rows).
    int content_height() const;
    // The rows shown on the main, keys and mods pages (tall_rows unless set: rows_for_screen).
    int rows_shown() const { return m_Rows; }
    void set_rows(int rows);
    int legend_y() const { return Layout::first_row_y + m_Rows * Layout::row_height + 2; }

    // The keys page (Row::Keys opens it): a row per action under its group's (the engine's, then the mods' of
    // KeyBindings::mod_actions as the window opens), then one that puts back all the game's keys; rows_shown of
    // them at a time, scrolled to the selected one.
    int key_row_count() const { return int(m_KeyRows.size()); }
    bool key_group(int row) const;
    // An action's row or the last (not a group's, not the empty one before the last).
    bool key_selectable(int row) const;
    bool keys_page() const { return m_KeysPage; }
    void close_keys();
    int key_selected() const { return m_KeySelected; }
    int key_top() const { return m_KeyTop; }
    bool capturing() const { return m_Capturing; }
    bool key_select(int row);
    void key_move(int delta);
    // The wheel: the page by rows, the selection kept.
    void key_scroll(int rows);
    bool key_more_above() const { return m_KeyTop > 0; }
    bool key_more_below() const { return m_KeyTop + m_Rows < key_row_count(); }
    // Enter or a click: an action waits for its key; the last row puts back the game's keys.
    void key_activate();
    // A key while waiting: Esc cancels, any other becomes the action's.
    void key_press(int vk);
    // Del: the selected action's key back to the game's.
    void key_default();
    std::wstring key_label(int row) const;
    std::wstring key_value(int row) const;
    // A key the person set that another action of its group has too (shown red, allowed: the game has such pairs).
    bool key_conflict(int row) const;
    std::wstring keys_title() const;
    std::wstring keys_legend() const;
    // The keys page's row under a point of the window, or -1.
    int key_hit(int x, int y) const;

    // The mods page (Row::Mods opens it; Esc back to the main page, its Mods row selected): the built-in personal
    // mods' rows, then for each personal mod of the mods folder a row (its name; grey when it cannot be used) and one
    // per setting of it, then Saved designs; scrolled as the keys page when they are more than fit.
    bool mods_page() const { return m_ModsPage; }
    void close_mods();
    const std::vector<int> &mods_rows() const { return m_ModsOrder; }
    // The main page's or the mods page's rows (-1 an empty one), the first shown, scrolled by the wheel.
    const std::vector<int> &page_rows() const { return m_ModsPage ? m_ModsOrder : m_MainOrder; }
    int page_top() const { return m_ModsPage ? m_ModsTop : m_MainTop; }
    void scroll(int rows);
    bool more_above() const { return page_top() > 0; }
    bool more_below() const { return page_top() + m_Rows < int(page_rows().size()); }
    // The shaders screens (Row::Shaders opens them): ShaderMenu's list and a pack's settings; their Done and Cancel
    // (close_shaders) back to the main page, Shaders selected.
    bool shaders_page() const { return m_ShadersPage; }
    void close_shaders();
    ShaderMenu::Menu &shader_menu() { return m_ShaderMenu; }
    const ShaderMenu::Menu &shader_menu() const { return m_ShaderMenu; }
    // A row of a mods folder's mod's setting (drawn further in).
    bool setting_row(int row) const;
    // A mod's own row (Far view, Interface fixes, First person, HD sky, a mods folder's mod): it has an "i".
    bool info_row(int row) const;
    // A mod's row that is on or off (all of them but Far view, which has its scales): a switch, no arrows.
    bool switch_row(int row) const;
    bool switched_on(int row) const;
    // The window a row's "i" opens.
    ModInfo::Info info(int row) const;
    // A mod switched on that needs personal mods that are off is not switched yet: the window asks question(), then
    // answer() switches it on with them (yes) or leaves it off (no).
    bool asking() const { return !m_Asking.empty(); }
    ModInfo::Question question() const;
    void answer(bool yes);
    // A mod's row on with a personal mod it cannot be on with (red; a battle does not start: the lobby says so).
    bool conflicting(int row) const;
    // The row under a point of the window on the page shown (as SettingsModel::hit).
    Hit hit(int x, int y) const;
    // A row's top on the page shown.
    int top(int row) const;

    int selected() const { return m_Selected; }
    // An option row that can be selected and changed (not one of a mod this PC lacks).
    bool enabled(int row) const;
    // A row the shader pack applied decides (ShaderPacks::game_setting): grey, with the pack's value.
    std::optional<bool> shader_decided(int row) const;
    Language language() const { return m_Language; }

    // Selects an option row of the page shown; group rows are ignored. Returns true when the selection changed.
    bool select(int row);
    void move(int delta);
    // Steps the value of an option row (of any page; an opener opens its page). Returns true when it changed.
    bool change(int row, int direction);


    void set_error(bool failed) { m_Error = failed; }
    bool error() const { return m_Error; }

    std::wstring title() const;
    std::wstring label(int row) const;
    std::wstring value(int row) const;
    std::wstring legend() const;
    std::wstring error_text() const;

private:
    StandaloneSettings::Values m_Values;
    StandaloneSettings::Values m_Original;
    std::vector<Resolution> m_Resolutions;
    Language m_Language;
    int m_Selected;
    bool m_Error = false;
    std::vector<std::string> m_DefaultPersonal;
    std::vector<std::string> m_Unavailable;
    KeyBindings::Keys m_Keys, m_KeysOriginal, m_KeysBase;
    bool m_KeysPage = false;
    bool m_ModsPage = false;
    bool m_Capturing = false;
    int m_KeySelected = 1;  // the first action (row 0 is its group's)
    int m_KeyTop = 0;
    // The keys page's rows: a group's, an engine action's (its index in KeyBindings::actions()), a mod's (in
    // m_ModActions), an empty one and the last, all keys back.
    struct KeyRow {
        int action = -1;
        int mod = -1;
        int group = -1;
        bool blank = false;
    };
    std::vector<KeyRow> m_KeyRows;
    std::vector<KeyBindings::ModAction> m_ModActions;
    // The mods folder's personal mods; their rows (row_count + index): a mod's own (setting -1) or a setting's.
    struct FolderRow {
        int mod;
        int setting;
    };
    std::vector<Mods::Mod> m_Mods;
    std::vector<Mods::Mod> m_Installed;  // all of them: the windows of the mods' rows name what they need
    std::vector<FolderRow> m_FolderRows;
    std::vector<int> m_ModsOrder;  // the mods page's rows in order
    int m_ModsTop = 0;
    std::vector<int> m_MainOrder = main_page();
    int m_MainTop = 0;
    int m_Rows = tall_rows;
    Mods::SettingValues m_ModSettings, m_ModSettingsOriginal;
    ShaderMenu::Menu m_ShaderMenu;
    std::wstring m_ShadersProblem;
    TextWidth m_TextWidth;
    bool m_ShadersPage = false;

    std::string m_Asking;  // the mod a question is about (asking)

    void fit_header();  // the shaders screens' Back and Reset: as wide as their texts (measured as the page opens)

    bool personal(const char *id) const;
    std::string row_mod(int row) const;  // the mod a row is (info_row); empty: none
    std::vector<std::string> personal_on() const;
    std::vector<Mods::Mod> personal_mods() const;  // the installed personal mods (what they need, what they conflict)
    void switch_mod(const std::string &id);
    void set_personal(const char *id, bool on);
    bool option(int row) const;
    void show(int row);  // the main page or the mods page scrolled to a row of it
    int key_of(const KeyRow &row, const KeyBindings::Keys &keys) const;
    void set_key(const KeyRow &row, int key);

    const wchar_t *text(const wchar_t *english, const wchar_t *russian) const;
};

// A hint text element of one row's height at a position in the window, also used by the Map window.
// align 0 is left-aligned and 1 is centred.
std::wstring text_element(const std::wstring &text, const wchar_t *font, uint32_t color, int x, int y, int width,
                          int align);

// CMatrixHint template for the window: the frame and button holes of the original confirmation
// dialogs, a blank block that sizes the frame, and text placed at the Layout coordinates.
std::wstring hint_template(const Window &window);

// Over the startup menu there is no battle interface to place the OK and Cancel buttons: the window
// draws text buttons under its options instead (from the window's content height on).
struct StartupLayout {
    static constexpr int buttons_room = 34;
    static constexpr int apply_x = 100;
    static constexpr int cancel_x = 230;
    static constexpr int button_width = 130;
};
std::wstring startup_template(const Window &window);
// 1 apply, -1 cancel, 0 neither (the shaders screens have their own buttons instead).
int startup_button(const Window &window, int x, int y);

// The framed boxes the shaders screens draw (their buttons, the pack chosen): runtime bitmaps of these names, a border
// line round a fill (SettingsWindow makes them before the window is built; the colours are the theme's).
struct Frame {
    std::wstring name;
    int width, height;
    uint32_t border, fill;  // ARGB
    bool operator==(const Frame &) const = default;
};
std::vector<Frame> frames(const Window &window);

}  // namespace SettingsModel
