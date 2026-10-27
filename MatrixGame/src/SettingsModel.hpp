// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "StandaloneSettings.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Device-independent state, text and geometry of the in-game settings window.
namespace SettingsModel {

enum class Language { English, Russian };

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
    Count
};

constexpr int row_count = static_cast<int>(Row::Count);

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
    static constexpr int row_height = 20;
    static constexpr int label_x = 70;
    static constexpr int previous_x = 250;  // "<" marker; the left half of the value cell steps back.
    static constexpr int value_x = 264;
    static constexpr int value_width = 136;
    static constexpr int next_x = 404;  // ">" marker; the right half of the value cell steps forward.
    static constexpr int cell_right = 420;
    static constexpr int legend_y = first_row_y + row_count * row_height + 6;
    static constexpr int content_height = legend_y + 2 * row_height;
};

struct Hit {
    int row = -1;       // Option row under the point, or -1.
    int direction = 0;  // -1 or +1 when the value cell was hit, otherwise 0.
};

bool is_group(int row);
bool needs_restart(int row);
// A battle shared by several people keeps its rules: rule rows then apply from the next battle.
void lock_battle_rules(bool locked);
Hit hit(int x, int y);
int row_top(int row);

// Adapter modes become a sorted, unique list of at least 640x480 that always contains the current size.
std::vector<Resolution> resolution_choices(std::vector<Resolution> modes, Resolution current);

// Russian when the game configuration's own menu text is Cyrillic.
Language detect_language(const std::wstring &menu_text);

class Window {
public:
    Window(StandaloneSettings::Values values, std::vector<Resolution> resolutions, Language language);

    const StandaloneSettings::Values &values() const { return m_Values; }
    bool changed() const { return m_Values != m_Original; }
    int selected() const { return m_Selected; }
    Language language() const { return m_Language; }

    // Selects an option row; group rows are ignored. Returns true when the selection changed.
    bool select(int row);
    void move(int delta);
    // Steps the value of an option row. Returns true when the value changed.
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
