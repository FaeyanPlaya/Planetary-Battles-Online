// MatrixGame - licensed under GPLv2 or any later version.
#include "SettingsModel.hpp"
#include "RandomConfiguration.hpp"

#include "utils.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace SettingsModel {
namespace {

using Display::Mode;
using StandaloneSettings::Skybox;

constexpr std::array window_modes{Mode::Windowed, Mode::Borderless, Mode::Exclusive};
constexpr std::array fullscreen_modes{Mode::Borderless, Mode::Exclusive};
constexpr std::array skyboxes{Skybox::Off, Skybox::Low, Skybox::High};
constexpr std::array far_views{0, 150, 200, 300};  // off, then the scale in percent

template<class T, size_t N>
T cycle(const std::array<T, N> &items, T current, int direction) {
    const auto found = std::ranges::find(items, current);
    const int index = found == items.end() ? 0 : static_cast<int>(found - items.begin());
    return items[(index + direction + static_cast<int>(N)) % static_cast<int>(N)];
}

// Moves to the neighboring multiple of step, so hand-edited values rejoin the grid.
int step(int value, int direction, int step, int minimum, int maximum) {
    const int next = direction > 0 ? (value / step + 1) * step : ((value + step - 1) / step - 1) * step;
    return std::clamp(next, minimum, maximum);
}

bool is_option(int row) { return row >= 0 && row < row_count && !is_group(row); }

constexpr uint32_t text_color = 0xFF000000;
constexpr uint32_t group_color = 0xFF3F6C8C;
constexpr uint32_t selected_color = 0xFF0B5FA5;
constexpr uint32_t arrow_color = 0xFF7D8890;
constexpr uint32_t legend_color = 0xFF5E676E;
constexpr uint32_t error_color = 0xFFB01818;
constexpr int option_indent = 12;
constexpr int text_height = 19;  // a text element's box (rows are closer: it may reach into the next)

std::wstring hex(uint32_t color) {
    static constexpr wchar_t digits[] = L"0123456789ABCDEF";
    std::wstring result(8, L'0');
    for (int i = 7; i >= 0; --i, color >>= 4) result[i] = digits[color & 0xf];
    return result;
}

}  // namespace

std::wstring text_element(const std::wstring &text, const wchar_t *font, uint32_t color, int x, int y, int width,
                          int align) {
    return L"|_FONT:" + std::wstring(font) + L"|_COLOR:" + hex(color) + L"|_WIDTH:" + std::to_wstring(width) +
           L"|_HEIGHT:" + std::to_wstring(text_height) + L"|_ALIGN:" + std::to_wstring(align) +
           L":0|_POS:" + std::to_wstring(x) + L":" + std::to_wstring(y) + L"|_TEXT:" + text;
}

bool is_group(int row) {
    const auto value = static_cast<Row>(row);
    return value == Row::DisplayGroup || value == Row::SoundGroup || value == Row::GraphicsGroup ||
           value == Row::GameplayGroup || value == Row::ModsGroup;
}

namespace {
bool g_battle_rules_locked = false;
}

void lock_battle_rules(bool locked) { g_battle_rules_locked = locked; }

bool needs_restart(int row) {
    const auto value = static_cast<Row>(row);
    return value == Row::Skybox || value == Row::RobotShadows || value == Row::RandomGenerator ||
           (value == Row::DelayedDamage && g_battle_rules_locked);
}

int row_top(int row) { return Layout::first_row_y + row * Layout::row_height; }

Hit hit(int x, int y) {
    if (y < Layout::first_row_y || x < Layout::label_x - 10 || x >= Layout::cell_right + 10) return {};
    const int row = (y - Layout::first_row_y) / Layout::row_height;
    if (!is_option(row)) return {};
    if (x < Layout::previous_x) return {row, 0};
    return {row, x < Layout::value_x + Layout::value_width / 2 ? -1 : 1};
}

std::vector<Resolution> resolution_choices(std::vector<Resolution> modes, Resolution current) {
    std::erase_if(modes, [](const Resolution &mode) { return mode.width < 640 || mode.height < 480; });
    modes.push_back(current);
    std::ranges::sort(modes, [](const Resolution &a, const Resolution &b) {
        return a.width != b.width ? a.width < b.width : a.height < b.height;
    });
    modes.erase(std::unique(modes.begin(), modes.end()), modes.end());
    return modes;
}

Language detect_language(const std::wstring &menu_text) {
    const bool cyrillic = std::ranges::any_of(menu_text, [](wchar_t ch) { return ch >= 0x0400 && ch <= 0x04ff; });
    return cyrillic ? Language::Russian : Language::English;
}

Window::Window(StandaloneSettings::Values values, std::vector<Resolution> resolutions, Language language,
               std::vector<std::string> default_personal)
  : m_Values(values), m_Original(values), m_Resolutions(std::move(resolutions)), m_Language(language),
    m_Selected(static_cast<int>(Row::Resolution)), m_DefaultPersonal(std::move(default_personal)) {
    m_Resolutions = resolution_choices(std::move(m_Resolutions), {values.display.width, values.display.height});
}

bool Window::personal(const char *id) const {
    const auto &ids = m_Values.personal_mods ? *m_Values.personal_mods : m_DefaultPersonal;
    return std::ranges::find(ids, id) != ids.end();
}

// The list is written out once a person chooses (PersonalMods= then names them).
void Window::set_personal(const char *id, bool on) {
    auto ids = m_Values.personal_mods ? *m_Values.personal_mods : m_DefaultPersonal;
    std::erase(ids, id);
    if (on) ids.emplace_back(id);
    m_Values.personal_mods = ids;
}

bool Window::select(int row) {
    if (!is_option(row) || row == m_Selected) return false;
    m_Selected = row;
    return true;
}

void Window::move(int delta) {
    int row = m_Selected;
    do {
        row = (row + delta + row_count) % row_count;
    } while (!is_option(row));
    m_Selected = row;
}

bool Window::change(int row, int direction) {
    if (!is_option(row) || direction == 0) return false;
    const auto before = m_Values;
    auto &values = m_Values;
    auto &display = values.display;
    switch (static_cast<Row>(row)) {
        case Row::Resolution: {
            const auto found = std::ranges::find(m_Resolutions, Resolution{display.width, display.height});
            const int index = std::clamp(static_cast<int>(found - m_Resolutions.begin()) + direction, 0,
                                         static_cast<int>(m_Resolutions.size()) - 1);
            display.width = m_Resolutions[index].width;
            display.height = m_Resolutions[index].height;
            break;
        }
        case Row::WindowMode:
            display.mode = cycle(window_modes, display.mode, direction);
            if (display.mode != Mode::Windowed) display.fullscreen = display.mode;
            break;
        case Row::FullscreenMode:
            display.fullscreen = cycle(fullscreen_modes, display.fullscreen, direction);
            // A fullscreen start always uses its own mode for Alt+Enter; keep both rows consistent.
            if (display.mode != Mode::Windowed) display.mode = display.fullscreen;
            break;
        case Row::SoundVolume: values.sound_volume = step(values.sound_volume, direction, 10, 0, 100); break;
        case Row::MusicVolume: values.music_volume = step(values.music_volume, direction, 10, 0, 100); break;
        case Row::StencilShadows: values.stencil_shadows = !values.stencil_shadows; break;
        case Row::ProjectedShadows: values.projected_shadows = !values.projected_shadows; break;
        case Row::Skybox: values.skybox = cycle(skyboxes, values.skybox, direction); break;
        case Row::RobotShadows: values.robot_shadows = !values.robot_shadows; break;
        case Row::FarView: {
            const int next = cycle(far_views, personal("farview") ? values.far_view_scale : 0, direction);
            set_personal("farview", next != 0);
            if (next != 0) values.far_view_scale = next;
            break;
        }
        case Row::InterfaceFixes: set_personal("interface", !personal("interface")); break;
        case Row::CameraSpeed:
            values.camera_speed = step(values.camera_speed, direction, 25, StandaloneSettings::min_camera_speed,
                                       StandaloneSettings::max_camera_speed);
            break;
        case Row::RandomGenerator:
            values.random = values.random == random::Mode::ParkMiller ? random::Mode::LegacyCRT
                                                                      : random::Mode::ParkMiller;
            break;
        case Row::DelayedDamage:
            values.delayed_damage = values.delayed_damage == StandaloneSettings::DamageAttribution::Corrected
                                            ? StandaloneSettings::DamageAttribution::Original
                                            : StandaloneSettings::DamageAttribution::Corrected;
            break;
        default: break;
    }
    return m_Values != before;
}

const wchar_t *Window::text(const wchar_t *english, const wchar_t *russian) const {
    return m_Language == Language::Russian ? russian : english;
}

std::wstring Window::title() const {
    return text(L"Settings", L"\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438");  // Nastroiki
}

std::wstring Window::label(int row) const {
    std::wstring result;
    switch (static_cast<Row>(row)) {
        case Row::DisplayGroup: result = text(L"Display", L"\u042d\u043a\u0440\u0430\u043d"); break;
        case Row::Resolution:
            result = text(L"Resolution", L"\u0420\u0430\u0437\u0440\u0435\u0448\u0435\u043d\u0438\u0435");
            break;
        case Row::WindowMode:
            result = text(L"Window mode", L"\u0420\u0435\u0436\u0438\u043c \u043e\u043a\u043d\u0430");
            break;
        case Row::FullscreenMode: result = L"Alt+Enter"; break;
        case Row::SoundGroup: result = text(L"Sound", L"\u0417\u0432\u0443\u043a"); break;
        case Row::SoundVolume:
            result = text(L"Volume", L"\u0413\u0440\u043e\u043c\u043a\u043e\u0441\u0442\u044c");
            break;
        case Row::MusicVolume: result = text(L"Music", L"\u041c\u0443\u0437\u044b\u043a\u0430"); break;
        case Row::GraphicsGroup: result = text(L"Graphics", L"\u0413\u0440\u0430\u0444\u0438\u043a\u0430"); break;
        case Row::StencilShadows:
            result = text(L"Stencil shadows",
                          L"\u041e\u0431\u044a\u0451\u043c\u043d\u044b\u0435 \u0442\u0435\u043d\u0438");
            break;
        case Row::ProjectedShadows:
            result = text(L"Projected shadows", L"\u041f\u0440\u043e\u0435\u043a\u0446\u0438\u043e\u043d\u043d"
                                                L"\u044b\u0435 \u0442\u0435\u043d\u0438");
            break;
        case Row::Skybox: result = text(L"Sky", L"\u041d\u0435\u0431\u043e"); break;
        case Row::RobotShadows:
            result = text(L"Robot shadows", L"\u0422\u0435\u043d\u0438 \u0440\u043e\u0431\u043e\u0442\u043e\u0432");
            break;
        case Row::FarView:
            result = text(L"Far view", L"\u0414\u0430\u043b\u044c\u043d\u044f\u044f \u043f\u0440\u043e\u0440\u0438\u0441\u043e\u0432\u043a\u0430");
            break;
        case Row::InterfaceFixes:
            // The mod's own name, Ispravleniya interfeisa, is wider than the label: Pravki interfeisa.
            result = text(L"Interface fixes", L"\u041f\u0440\u0430\u0432\u043a\u0438 \u0438\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441\u0430");
            break;
        case Row::GameplayGroup: result = text(L"Gameplay", L"\u0418\u0433\u0440\u0430"); break;
        case Row::ModsGroup: result = text(L"Mods", L"\u041c\u043e\u0434\u044b"); break;
        case Row::CameraSpeed:
            result = text(L"Camera speed", L"\u0421\u043a\u043e\u0440\u043e\u0441\u0442\u044c \u043a\u0430\u043c"
                                           L"\u0435\u0440\u044b");
            break;
        case Row::RandomGenerator:
            result = text(L"Random generator", L"\u0421\u043b\u0443\u0447\u0430\u0439\u043d\u044b\u0435 \u0447"
                                               L"\u0438\u0441\u043b\u0430");
            break;
        case Row::DelayedDamage:
            result = text(L"Status damage",
                          L"\u0423\u0440\u043e\u043d \u0441\u0442\u0430\u0442\u0443\u0441\u043e\u0432");
            break;
        default: break;
    }
    if (needs_restart(row)) result += L" *";
    return result;
}

std::wstring Window::value(int row) const {
    const auto on_off = [this](bool on) {
        return std::wstring(on ? text(L"On", L"\u0412\u043a\u043b") : text(L"Off", L"\u0412\u044b\u043a\u043b"));
    };
    const auto mode = [this](Mode value) -> std::wstring {
        switch (value) {
            case Mode::Windowed: return text(L"Windowed", L"\u041e\u043a\u043e\u043d\u043d\u044b\u0439");
            case Mode::Borderless: return text(L"Borderless", L"\u0411\u0435\u0437 \u0440\u0430\u043c\u043a\u0438");
            case Mode::Exclusive:
                return text(L"Exclusive", L"\u041f\u043e\u043b\u043d\u044b\u0439 \u044d\u043a\u0440\u0430\u043d");
        }
        return {};
    };
    const auto &values = m_Values;
    switch (static_cast<Row>(row)) {
        case Row::Resolution:
            return std::to_wstring(values.display.width) + L"x" + std::to_wstring(values.display.height);
        case Row::WindowMode: return mode(values.display.mode);
        case Row::FullscreenMode: return mode(values.display.fullscreen);
        case Row::SoundVolume: return std::to_wstring(values.sound_volume) + L"%";
        case Row::MusicVolume: return std::to_wstring(values.music_volume) + L"%";
        case Row::StencilShadows: return on_off(values.stencil_shadows);
        case Row::ProjectedShadows: return on_off(values.projected_shadows);
        case Row::Skybox:
            switch (values.skybox) {
                case Skybox::Off: return on_off(false);
                case Skybox::Low: return text(L"Low", L"\u041d\u0438\u0437\u043a\u043e\u0435");
                case Skybox::High: return text(L"High", L"\u0412\u044b\u0441\u043e\u043a\u043e\u0435");
            }
            return {};
        case Row::RobotShadows: return on_off(values.robot_shadows);
        case Row::FarView: {
            if (!personal("farview")) return on_off(false);
            const int scale = values.far_view_scale;
            std::wstring times = std::to_wstring(scale / 100);
            if (scale % 100) times += (m_Language == Language::Russian ? L"," : L".") + std::to_wstring(scale % 100 / 10);
            return L"x" + times;
        }
        case Row::InterfaceFixes: return on_off(personal("interface"));
        case Row::CameraSpeed: return std::to_wstring(values.camera_speed) + L"%";
        case Row::RandomGenerator: return utils::to_wstring(RandomConfiguration::name(values.random));
        case Row::DelayedDamage:
            return values.delayed_damage == StandaloneSettings::DamageAttribution::Original
                           ? text(L"Original", L"\u041e\u0440\u0438\u0433\u0438\u043d\u0430\u043b")
                           : text(L"Corrected", L"\u0418\u0441\u043f\u0440\u0430\u0432\u043b\u0435\u043d");
        default: return {};
    }
}

std::wstring Window::legend() const {
    return text(L"* Applies after restart", L"* \u041f\u0440\u0438\u043c\u0435\u043d\u044f\u0435\u0442\u0441\u044f "
                                            L"\u043f\u043e\u0441\u043b\u0435 \u043f\u0435\u0440\u0435\u0437\u0430"
                                            L"\u043f\u0443\u0441\u043a\u0430");
}

std::wstring Window::error_text() const {
    return text(L"Settings could not be saved; see test.log",
                L"\u041d\u0435 \u0443\u0434\u0430\u043b\u043e\u0441\u044c \u0441\u043e\u0445\u0440\u0430\u043d\u0438"
                L"\u0442\u044c \u043d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438, \u0441\u043c. test.log");
}

namespace {
std::wstring options_text(const Window &window);
}

std::wstring hint_template(const Window &window) {
    // The blank block sizes the frame. The copied holes are transparent, so the OK and Cancel hint
    // buttons placed at their positions show through, as in the Exit/Reset/Surrender confirmations.
    return std::to_wstring(window_border) + L"|_BORDER:0:0:0:28|_FONT:Font.2Normal|_WIDTH:" + std::to_wstring(Layout::width) + L"|_HEIGHT:" +
           std::to_wstring(Layout::content_height) +
           L"|_TEXTP:L|_TEXT: |_MOD:COPY|_BITMAP:bhole2:CL:6|_MOD:COPY|_BITMAP:bhole2:CR:6|_TEXTP:B" + options_text(window);
}

std::wstring startup_template(const Window &window) {
    std::wstring result = std::to_wstring(window_border) + L"|_BORDER:0:0:0:0|_FONT:Font.2Normal|_WIDTH:" + std::to_wstring(Layout::width) + L"|_HEIGHT:" +
                          std::to_wstring(StartupLayout::content_height) + L"|_TEXTP:L|_TEXT: |_TEXTP:B" + options_text(window);
    const bool russian = window.language() == Language::Russian;
    result += text_element(russian ? L"( \u041f\u0440\u0438\u043c\u0435\u043d\u0438\u0442\u044c )" : L"( Apply )",
                           L"Font.2Normal", selected_color, StartupLayout::apply_x, StartupLayout::buttons_y,
                           StartupLayout::button_width, 1);
    result += text_element(russian ? L"( \u041e\u0442\u043c\u0435\u043d\u0430 )" : L"( Cancel )", L"Font.2Normal",
                           selected_color, StartupLayout::cancel_x, StartupLayout::buttons_y, StartupLayout::button_width, 1);
    return result;
}

int startup_button(int x, int y) {
    if (y < StartupLayout::buttons_y || y >= StartupLayout::buttons_y + Layout::row_height) return 0;
    if (x >= StartupLayout::apply_x && x < StartupLayout::apply_x + StartupLayout::button_width) return 1;
    if (x >= StartupLayout::cancel_x && x < StartupLayout::cancel_x + StartupLayout::button_width) return -1;
    return 0;
}

namespace {
std::wstring options_text(const Window &window) {
    std::wstring result;
    result += text_element(window.title(), L"Font.2Ranger", text_color, 0, Layout::title_y, Layout::width, 1);
    for (int row = 0; row < row_count; ++row) {
        const int y = row_top(row);
        if (is_group(row)) {
            result += text_element(window.label(row), L"Font.2Small", group_color, Layout::label_x, y,
                                   Layout::previous_x - Layout::label_x, 0);
            continue;
        }
        const bool selected = row == window.selected();
        const uint32_t color = selected ? selected_color : text_color;
        const uint32_t arrows = selected ? selected_color : arrow_color;
        result += text_element(window.label(row), L"Font.2Normal", color, Layout::label_x + option_indent, y,
                               Layout::previous_x - Layout::label_x - option_indent, 0);
        result += text_element(L"<", L"Font.2Normal", arrows, Layout::previous_x, y,
                               Layout::value_x - Layout::previous_x, 0);
        result += text_element(window.value(row), L"Font.2Normal", color, Layout::value_x, y, Layout::value_width, 1);
        result += text_element(L">", L"Font.2Normal", arrows, Layout::next_x, y, Layout::cell_right - Layout::next_x,
                               0);
    }
    result += text_element(window.legend(), L"Font.2Small", legend_color, Layout::label_x, Layout::legend_y,
                           Layout::cell_right - Layout::label_x, 0);
    if (window.error()) {
        result += text_element(window.error_text(), L"Font.2Small", error_color, Layout::label_x,
                               Layout::legend_y + Layout::row_height, Layout::cell_right - Layout::label_x, 0);
    }
    return result;
}
}  // namespace

}  // namespace SettingsModel
