// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "StandaloneSettings.hpp"
#include "KeyBindings.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Device-independent state, text and geometry of the in-game settings window.
namespace SettingsModel {

enum class Language { English, Russian };

// The frame of this project's windows (the hint markup's first number): the game's large panel without its help mark
// (CMatrixHint makes it from border 2).
constexpr int window_border = 5;

enum class Row {
    DisplayGroup,
    Resolution,
    WindowMode,
    FullscreenMode,
    SoundGroup,
    SoundVolume,
    MusicVolume,
    GraphicsGroup,
    StencilShadows,
    ProjectedShadows,
    Skybox,
    RobotShadows,
    GameplayGroup,
    CameraSpeed,
    RandomGenerator,
    DelayedDamage,
    Keys,            // opens the keys page (18, key rebinding)
    Mods,            // opens the mods page, this person's own mods (the rows after it)
    FarView,         // farview and its scale
    InterfaceFixes,  // interface
    FirstPerson,     // firstperson
    NewSky,          // newsky: grey and Off while the game lacks PBNewSky
    SavedDesigns,    // how many designs built are kept between battles (the engine's, not a mod)
    Count
};

constexpr int row_count = static_cast<int>(Row::Count);
// The main page has the rows up to Mods, the mods page the rest; the window is main_rows high on every page.
constexpr int main_rows = static_cast<int>(Row::Mods) + 1;
constexpr int mods_first = static_cast<int>(Row::FarView);

struct Resolution {
    int width;
    int height;
    bool operator==(const Resolution &) const = default;
};

// Window geometry in pixels, relative to the window's top-left corner.
struct Layout {
    static constexpr int width = 460;
    static constexpr int title_y = 30;
    static constexpr int first_row_y = 58;
    static constexpr int row_height = 17;  // (15 while the mods were rows of the main page: 640x480)
    static constexpr int label_x = 70;
    static constexpr int previous_x = 250;  // "<" marker; the left half of the value cell steps back.
    static constexpr int value_x = 264;
    static constexpr int value_width = 136;
    static constexpr int next_x = 404;  // ">" marker; the right half of the value cell steps forward.
    static constexpr int cell_right = 420;
    static constexpr int legend_y = first_row_y + main_rows * row_height + 5;
    static constexpr int content_height = legend_y + row_height;  // (a failed save's text in the legend's place)
};

struct Hit {
    int row = -1;       // Option row under the point, or -1.
    int direction = 0;  // -1 or +1 when the value cell was hit, otherwise 0.
};

bool is_group(int row);
bool mods_row(int row);
// A row that opens a page (Keys, Mods): no arrows, any click or Enter opens it.
bool opener(int row);
bool needs_restart(int row);
// A battle shared by several people keeps its rules: rule rows then apply from the next battle.
void lock_battle_rules(bool locked);
// On the main page or the mods page (rows by their place on their page).
Hit hit(int x, int y, bool mods_page = false);
int row_top(int row);

// Adapter modes become a sorted, unique list of at least 640x480 that always contains the current size.
std::vector<Resolution> resolution_choices(std::vector<Resolution> modes, Resolution current);

// Russian when the game configuration's own menu text is Cyrillic.
Language detect_language(const std::wstring &menu_text);

class Window {
public:
    // default_personal: the personal mods of a person who never chose (PersonalMods=Default); unavailable: personal
    // mods whose files this PC lacks (their rows grey, Off and fixed; the choice kept for when they are back).
    // keys: the person's keys now (the keys page changes them; base: the game's, what Del puts back).
    Window(StandaloneSettings::Values values, std::vector<Resolution> resolutions, Language language,
           std::vector<std::string> default_personal = {}, std::vector<std::string> unavailable = {},
           KeyBindings::Keys keys = KeyBindings::base(), KeyBindings::Keys base = KeyBindings::base());

    const StandaloneSettings::Values &values() const { return m_Values; }
    bool changed() const { return m_Values != m_Original || m_Keys != m_KeysOriginal; }
    const KeyBindings::Keys &keys() const { return m_Keys; }
    bool keys_changed() const { return m_Keys != m_KeysOriginal; }

    // The keys page (Row::Keys opens it): a row per action under its group's, then one that puts back all the
    // game's keys; key_rows_shown of them at a time, scrolled to the selected one.
    static constexpr int key_rows_shown = main_rows;
    static int key_row_count();
    static bool key_group(int row);
    // An action's row or the last (not a group's, not the empty one before the last).
    static bool key_selectable(int row);
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
    bool key_more_below() const { return m_KeyTop + key_rows_shown < key_row_count(); }
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

    // The mods page (Row::Mods opens it; Esc back to the main page, its Mods row selected).
    bool mods_page() const { return m_ModsPage; }
    void close_mods();

    int selected() const { return m_Selected; }
    // An option row that can be selected and changed (not one of a mod this PC lacks).
    bool enabled(int row) const;
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

    bool personal(const char *id) const;
    void set_personal(const char *id, bool on);

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
// draws text buttons under its options instead.
struct StartupLayout {
    static constexpr int buttons_y = Layout::content_height;
    static constexpr int content_height = buttons_y + 34;
    static constexpr int apply_x = 100;
    static constexpr int cancel_x = 230;
    static constexpr int button_width = 130;
};
std::wstring startup_template(const Window &window);
// 1 apply, -1 cancel, 0 neither.
int startup_button(int x, int y);

}  // namespace SettingsModel
