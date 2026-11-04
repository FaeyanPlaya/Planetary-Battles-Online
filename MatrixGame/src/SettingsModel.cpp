// MatrixGame - licensed under GPLv2 or any later version.
#include "SettingsModel.hpp"
#include "UiTheme.hpp"
#include "ModInfo.hpp"
#include "RandomConfiguration.hpp"

#include "utils.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

namespace SettingsModel {
namespace {

using Display::Mode;
using StandaloneSettings::Skybox;

constexpr std::array window_modes{Mode::Windowed, Mode::Borderless, Mode::Exclusive};
constexpr std::array fullscreen_modes{Mode::Borderless, Mode::Exclusive};
constexpr std::array skyboxes{Skybox::Off, Skybox::Low, Skybox::High};
constexpr std::array themes{StandaloneSettings::Theme::System, StandaloneSettings::Theme::Light, StandaloneSettings::Theme::Dark};
constexpr std::array far_views{0, 150, 200, 300};  // off, then the scale in percent
constexpr std::array saved_designs{1, 10, 20, 30, 40, 50};

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

uint32_t text_color() { return UiTheme::pick(0xFF000000, UiTheme::Dark::text); }
uint32_t group_color() { return UiTheme::pick(0xFF3F6C8C, UiTheme::Dark::group); }
uint32_t selected_color() { return UiTheme::pick(0xFF0B5FA5, UiTheme::Dark::selected); }
uint32_t arrow_color() { return UiTheme::pick(0xFF7D8890, UiTheme::Dark::arrow); }
uint32_t legend_color() { return UiTheme::pick(0xFF5E676E, UiTheme::Dark::legend); }
uint32_t error_color() { return UiTheme::pick(0xFFB01818, UiTheme::Dark::error); }
uint32_t disabled_color() { return UiTheme::pick(0xFF9AA1A7, UiTheme::Dark::disabled); }
constexpr const wchar_t *up_arrow = L"\u2191", *down_arrow = L"\u2193";  // the keys page scrolls that way
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

std::wstring switch_bitmap(bool on) { return on ? L"SettingsSwitchOn" : L"SettingsSwitchOff"; }
std::wstring info_bitmap() { return L"SettingsInfo"; }

bool is_group(int row) {
    const auto value = static_cast<Row>(row);
    return value == Row::DisplayGroup || value == Row::SoundGroup || value == Row::GraphicsGroup ||
           value == Row::GameplayGroup;
}

bool mods_row(int row) { return row >= mods_first && row < row_count; }

bool opener(int row) { return row == static_cast<int>(Row::Keys) || row == static_cast<int>(Row::Mods); }

namespace {
bool g_battle_rules_locked = false;
}

void lock_battle_rules(bool locked) { g_battle_rules_locked = locked; }

bool needs_restart(int row) {
    const auto value = static_cast<Row>(row);
    // NewSky: the sky is loaded with the map, so from the next battle; SavedDesigns: a battle loads that many.
    return value == Row::Skybox || value == Row::RobotShadows || value == Row::RandomGenerator || value == Row::NewSky ||
           value == Row::SavedDesigns || (value == Row::DelayedDamage && g_battle_rules_locked);
}

int row_top(int row) { return Layout::first_row_y + (mods_row(row) ? row - mods_first : row) * Layout::row_height; }

Hit hit(int x, int y, bool mods_page) {
    if (y < Layout::first_row_y || x < Layout::label_x - 10 || x >= Layout::cell_right + 10) return {};
    const int place = (y - Layout::first_row_y) / Layout::row_height;
    const int row = mods_page ? mods_first + place : place;
    if (!is_option(row) || mods_row(row) != mods_page) return {};
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
               std::vector<std::string> default_personal, std::vector<std::string> unavailable,
               KeyBindings::Keys keys, KeyBindings::Keys base, const std::vector<Mods::Mod> &mods,
               Mods::SettingValues mod_settings)
  : m_Values(values), m_Original(values), m_Resolutions(std::move(resolutions)), m_Language(language),
    m_Selected(static_cast<int>(Row::Resolution)), m_DefaultPersonal(std::move(default_personal)),
    m_Unavailable(std::move(unavailable)), m_Keys(keys), m_KeysOriginal(keys), m_KeysBase(base),
    m_Installed(mods), m_ModSettings(mod_settings), m_ModSettingsOriginal(std::move(mod_settings)) {
    m_Resolutions = resolution_choices(std::move(m_Resolutions), {values.display.width, values.display.height});
    // The keys page: each group's engine actions, then its mods' ones.
    const auto actions = KeyBindings::actions();
    m_ModActions.assign(KeyBindings::mod_actions().begin(), KeyBindings::mod_actions().end());
    for (int group = 0; group <= int(KeyBindings::Group::Mods); ++group) {
        const std::size_t first = m_KeyRows.size();
        const auto add = [&](int action, int mod) {
            if (m_KeyRows.size() == first) m_KeyRows.push_back({-1, -1, group});
            m_KeyRows.push_back({action, mod, group});
        };
        for (int i = 0; i < int(actions.size()); ++i)
            if (int(actions[i].group) == group) add(i, -1);
        for (int i = 0; i < int(m_ModActions.size()); ++i)
            if (int(m_ModActions[i].group) == group) add(-1, i);
    }
    m_KeyRows.push_back({-1, -1, -1, true});
    m_KeyRows.push_back({});
    // The mods page: the mods folder's personal mods (not those with rows of their own) before Saved designs.
    for (const auto &mod : mods) {
        const bool fixed = mod.id == "farview" || mod.id == "interface" || mod.id == "firstperson" || mod.id == "newsky";
        if (mod.kind != Mods::Kind::Personal || mod.folder.empty() || fixed) continue;
        const int index = int(m_Mods.size());
        m_Mods.push_back(mod);
        if (mod.problem != Mods::Problem::None) m_Unavailable.push_back(mod.id);
        m_FolderRows.push_back({index, -1});
        for (int s = 0; s < int(mod.settings.size()); ++s) m_FolderRows.push_back({index, s});
    }
    for (int row = mods_first; row < row_count; ++row) {
        if (row == static_cast<int>(Row::SavedDesigns))
            for (int i = 0; i < int(m_FolderRows.size()); ++i) m_ModsOrder.push_back(row_count + i);
        m_ModsOrder.push_back(row);
    }
}

bool Window::option(int row) const {
    return (row >= 0 && row < row_count && !is_group(row)) || (row >= row_count && row < row_count + int(m_FolderRows.size()));
}

std::string Window::row_mod(int row) const {
    if (row >= row_count) return option(row) && !setting_row(row) ? m_Mods[m_FolderRows[row - row_count].mod].id : std::string();
    switch (static_cast<Row>(row)) {
        case Row::FarView: return "farview";
        case Row::InterfaceFixes: return "interface";
        case Row::FirstPerson: return "firstperson";
        case Row::NewSky: return "newsky";
        default: return {};
    }
}

bool Window::info_row(int row) const { return !row_mod(row).empty(); }

bool Window::switch_row(int row) const { return info_row(row) && row != static_cast<int>(Row::FarView); }

bool Window::switched_on(int row) const {
    const std::string id = row_mod(row);
    return !id.empty() && personal(id.c_str());
}

ModInfo::Info Window::info(int row) const {
    const std::string id = row_mod(row);
    const auto found = std::ranges::find(m_Installed, id, &Mods::Mod::id);
    const Mods::Mod *mod = found != m_Installed.end() ? &*found : nullptr;
    ModInfo::Info info;
    if (mod) info = ModInfo::of(*mod, m_Installed, m_Language);
    else {
        info.name = label(row);
        info.kind = ModInfo::Info::Kind::Personal;
    }
    if (!mod)
        info.problem = text(L"Not installed: its folder is not in mods beside the game",
                            L"\u041d\u0435 \u0443\u0441\u0442\u0430\u043d\u043e\u0432\u043b\u0435\u043d: \u0435\u0433\u043e \u043f\u0430\u043f\u043a\u0438 \u043d\u0435\u0442 \u0432 mods \u0440\u044f\u0434\u043e\u043c \u0441 \u0438\u0433\u0440\u043e\u0439");
    else if (info.problem.empty() && std::ranges::find(m_Unavailable, id) != m_Unavailable.end())  // HD sky: PBNewSky
        info.problem = text(L"The game has no PBNewSky (Universe), whose skies it shows",
                            L"\u0412 \u0438\u0433\u0440\u0435 \u043d\u0435\u0442 PBNewSky (Universe): \u043d\u0435\u0431\u043e \u0431\u0435\u0440\u0451\u0442\u0441\u044f \u0438\u0437 \u043d\u0435\u0433\u043e");
    else if (info.problem.empty() && conflicting(row))
        info.problem = text(L"On with a mod it conflicts with: battles do not start",
                            L"\u0412\u043a\u043b\u044e\u0447\u0451\u043d \u0432\u043c\u0435\u0441\u0442\u0435 \u0441 \u043d\u0435\u0441\u043e\u0432\u043c\u0435\u0441\u0442\u0438\u043c\u044b\u043c \u043c\u043e\u0434\u043e\u043c: \u0431\u043e\u0439 \u043d\u0435 \u043d\u0430\u0447\u043d\u0451\u0442\u0441\u044f");
    return info;
}

bool Window::setting_row(int row) const {
    return row >= row_count && row < row_count + int(m_FolderRows.size()) && m_FolderRows[row - row_count].setting >= 0;
}

void Window::mods_show(int row) {
    const auto found = std::ranges::find(m_ModsOrder, row);
    if (found == m_ModsOrder.end()) return;
    const int place = int(found - m_ModsOrder.begin());
    if (place < m_ModsTop) m_ModsTop = place;
    if (place >= m_ModsTop + mods_rows_shown) m_ModsTop = place - mods_rows_shown + 1;
}

void Window::mods_scroll(int rows) {
    m_ModsTop = std::clamp(m_ModsTop + rows, 0, std::max(0, int(m_ModsOrder.size()) - mods_rows_shown));
}

int Window::top(int row) const {
    if (!m_ModsPage) return row_top(row);
    const auto found = std::ranges::find(m_ModsOrder, row);
    return Layout::first_row_y + (int(found - m_ModsOrder.begin()) - m_ModsTop) * Layout::row_height;
}

Hit Window::hit(int x, int y) const {
    if (!m_ModsPage) return SettingsModel::hit(x, y);
    if (y < Layout::first_row_y || x < Layout::label_x - 10 || x >= Layout::cell_right + 10) return {};
    const int place = (y - Layout::first_row_y) / Layout::row_height;
    if (place >= mods_rows_shown || m_ModsTop + place >= int(m_ModsOrder.size())) return {};
    const int row = m_ModsOrder[m_ModsTop + place];
    if (!option(row)) return {};
    if (info_row(row) && x >= Layout::info_x && x < Layout::info_x + Layout::button_size) return {row, 0, true};
    if (x < Layout::previous_x) return {row, 0};
    return {row, x < Layout::value_x + Layout::value_width / 2 ? -1 : 1};
}

std::vector<std::string> Window::personal_on() const {
    return m_Values.personal_mods ? *m_Values.personal_mods : m_DefaultPersonal;
}

std::vector<Mods::Mod> Window::personal_mods() const {
    std::vector<Mods::Mod> mods;
    for (const auto &mod : m_Installed)
        if (mod.kind == Mods::Kind::Personal) mods.push_back(mod);
    return mods;
}

// On or off with what it needs (Mods::toggled); on with personal mods it needs that are off: asked first.
void Window::switch_mod(const std::string &id) {
    const auto ids = personal_on();
    if (std::ranges::find(ids, id) == ids.end() && !Mods::needs(personal_mods(), ids, id).empty()) {
        m_Asking = id;
        return;
    }
    m_Values.personal_mods = Mods::toggled(personal_mods(), ids, id);
}

ModInfo::Question Window::question() const {
    ModInfo::Question question;
    const bool russian = m_Language == Language::Russian;
    const auto name = [&](const std::string &id) {
        const auto found = std::ranges::find(m_Installed, id, &Mods::Mod::id);
        return found == m_Installed.end() ? std::wstring(id.begin(), id.end()) : russian ? found->name : found->name_en;
    };
    question.mod = name(m_Asking);
    for (const auto &id : Mods::needs(personal_mods(), personal_on(), m_Asking)) question.needs.push_back(name(id));
    return question;
}

void Window::answer(bool yes) {
    const std::string id = std::exchange(m_Asking, {});
    const auto ids = personal_on();
    if (yes && !id.empty() && std::ranges::find(ids, id) == ids.end())
        m_Values.personal_mods = Mods::toggled(personal_mods(), ids, id);
}

bool Window::conflicting(int row) const {
    const std::string id = row_mod(row);
    if (id.empty() || !personal(id.c_str())) return false;
    for (const auto &[a, b] : Mods::conflicting(personal_mods(), personal_on()))
        if (a == id || b == id) return true;
    return false;
}

bool Window::personal(const char *id) const {
    if (std::ranges::find(m_Unavailable, id) != m_Unavailable.end()) return false;  // shown Off
    const auto &ids = m_Values.personal_mods ? *m_Values.personal_mods : m_DefaultPersonal;
    return std::ranges::find(ids, id) != ids.end();
}

bool Window::enabled(int row) const {
    if (!option(row)) return false;
    if (row >= row_count) return m_Mods[m_FolderRows[row - row_count].mod].problem == Mods::Problem::None;
    const auto value = static_cast<Row>(row);
    const char *id = value == Row::NewSky           ? "newsky"
                     : value == Row::InterfaceFixes ? "interface"
                     : value == Row::FarView        ? "farview"
                     : value == Row::FirstPerson    ? "firstperson"
                                                    : nullptr;
    return !(id && std::ranges::find(m_Unavailable, id) != m_Unavailable.end());
}

// The list is written out once a person chooses (PersonalMods= then names them).
void Window::set_personal(const char *id, bool on) {
    auto ids = m_Values.personal_mods ? *m_Values.personal_mods : m_DefaultPersonal;
    std::erase(ids, id);
    if (on) ids.emplace_back(id);
    m_Values.personal_mods = ids;
}

bool Window::select(int row) {
    if (!enabled(row) || (mods_row(row) || row >= row_count) != m_ModsPage || row == m_Selected) return false;
    m_Selected = row;
    if (m_ModsPage) mods_show(row);
    return true;
}

void Window::move(int delta) {
    if (m_ModsPage) {
        const int count = int(m_ModsOrder.size());
        int place = int(std::ranges::find(m_ModsOrder, m_Selected) - m_ModsOrder.begin());
        do {
            place = (place + delta + count) % count;
        } while (!enabled(m_ModsOrder[place]));
        m_Selected = m_ModsOrder[place];
        mods_show(m_Selected);
        return;
    }
    const int first = m_ModsPage ? mods_first : 0;
    const int count = m_ModsPage ? row_count - mods_first : main_rows;
    int row = m_Selected;
    do {
        row = first + (row - first + delta + count) % count;
    } while (!enabled(row));
    m_Selected = row;
}

void Window::close_mods() {
    m_ModsPage = false;
    m_Selected = static_cast<int>(Row::Mods);
}

bool Window::change(int row, int direction) {
    if (row == static_cast<int>(Row::Keys)) {  // either arrow, Enter or a click opens it
        m_KeysPage = true;
        m_Capturing = false;
        return true;
    }
    if (row == static_cast<int>(Row::Mods)) {
        m_ModsPage = true;
        m_Selected = mods_first;
        m_ModsTop = 0;
        return true;
    }
    if (!enabled(row) || direction == 0) return false;
    if (row >= row_count) {  // a mods folder's mod: on and off, or its setting's values in turn
        const FolderRow &place = m_FolderRows[row - row_count];
        const Mods::Mod &mod = m_Mods[place.mod];
        if (place.setting < 0) {
            switch_mod(mod.id);
            return true;
        }
        const Mods::SettingLine &setting = mod.settings[place.setting];
        const auto before = m_ModSettings;
        const int count = int(setting.values.size());
        const auto now = std::ranges::find(setting.values, Mods::setting_value(mod, setting, m_ModSettings));
        const int index = (int(now - setting.values.begin()) + direction + count) % count;
        m_ModSettings[mod.id + "." + setting.id] = setting.values[index];
        return m_ModSettings != before;
    }
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
        case Row::Theme: values.theme = cycle(themes, values.theme, direction); break;
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
        case Row::InterfaceFixes: switch_mod("interface"); break;
        case Row::FirstPerson: switch_mod("firstperson"); break;
        case Row::NewSky: switch_mod("newsky"); break;
        case Row::SavedDesigns: {
            // The neighbouring choice, a hand-written count between them rejoining them; no wrap.
            int &kept = values.saved_designs;
            if (direction > 0) {
                const auto next = std::ranges::find_if(saved_designs, [&](int n) { return n > kept; });
                if (next != saved_designs.end()) kept = *next;
            }
            else {
                const auto next = std::ranges::find_if(saved_designs.rbegin(), saved_designs.rend(),
                                                       [&](int n) { return n < kept; });
                if (next != saved_designs.rend()) kept = *next;
            }
            break;
        }
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
    if (m_ModsPage) return text(L"Mods", L"\u041c\u043e\u0434\u044b");  // Mody
    return text(L"Settings", L"\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438");  // Nastroiki
}

std::wstring Window::label(int row) const {
    if (row >= row_count && option(row)) {
        const FolderRow &place = m_FolderRows[row - row_count];
        const Mods::Mod &mod = m_Mods[place.mod];
        const bool russian = m_Language == Language::Russian;
        if (place.setting < 0) return russian || mod.name_en.empty() ? mod.name : mod.name_en;
        const Mods::SettingLine &setting = mod.settings[place.setting];
        return russian || setting.english.empty() ? setting.russian : setting.english;
    }
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
        case Row::Theme: result = text(L"Theme", L"\u0422\u0435\u043c\u0430"); break;
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
        case Row::FirstPerson:
            result = text(L"First person view", L"\u0412\u0438\u0434 \u043e\u0442 \u043f\u0435\u0440\u0432\u043e\u0433\u043e \u043b\u0438\u0446\u0430");
            break;
        case Row::NewSky: result = text(L"HD sky", L"\u041d\u0435\u0431\u043e HD"); break;  // Nebo HD
        case Row::SavedDesigns:
            result = text(L"Saved designs", L"\u0421\u043e\u0445\u0440\u0430\u043d\u044f\u0442\u044c \u0447\u0435\u0440\u0442\u0435\u0436\u0438");  // Sokhraniat chertezhi
            break;
        case Row::GameplayGroup: result = text(L"Gameplay", L"\u0418\u0433\u0440\u0430"); break;
        case Row::Keys: result = text(L"Keys", L"\u041a\u043b\u0430\u0432\u0438\u0448\u0438"); break;  // Klavishi
        case Row::Mods: result = text(L"Mods", L"\u041c\u043e\u0434\u044b"); break;
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
    if (row >= row_count && option(row)) {
        const FolderRow &place = m_FolderRows[row - row_count];
        const Mods::Mod &mod = m_Mods[place.mod];
        if (place.setting < 0) return on_off(personal(mod.id.c_str()));
        return Mods::setting_value(mod, mod.settings[place.setting], m_ModSettings);
    }
    const auto &values = m_Values;
    switch (static_cast<Row>(row)) {
        case Row::Resolution:
            return std::to_wstring(values.display.width) + L"x" + std::to_wstring(values.display.height);
        case Row::WindowMode: return mode(values.display.mode);
        case Row::FullscreenMode: return mode(values.display.fullscreen);
        case Row::Theme:
            switch (values.theme) {
                case StandaloneSettings::Theme::System: return text(L"As Windows", L"\u041a\u0430\u043a \u0432 \u0441\u0438\u0441\u0442\u0435\u043c\u0435");
                case StandaloneSettings::Theme::Light: return text(L"Light", L"\u0421\u0432\u0435\u0442\u043b\u0430\u044f");
                case StandaloneSettings::Theme::Dark: return text(L"Dark", L"\u0422\u0451\u043c\u043d\u0430\u044f");
            }
            return {};
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
        case Row::FirstPerson: return on_off(personal("firstperson"));
        case Row::NewSky: return on_off(personal("newsky"));
        case Row::SavedDesigns: return std::to_wstring(values.saved_designs);
        case Row::Keys:
        case Row::Mods: return text(L"Change", L"\u0418\u0437\u043c\u0435\u043d\u0438\u0442\u044c");  // Izmenit'
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

bool Window::key_group(int row) const {
    return row >= 0 && row < key_row_count() && m_KeyRows[row].action < 0 && m_KeyRows[row].mod < 0 &&
           m_KeyRows[row].group >= 0;
}

bool Window::key_selectable(int row) const {
    return row >= 0 && row < key_row_count() && !key_group(row) && !m_KeyRows[row].blank;
}

int Window::key_of(const KeyRow &row, const KeyBindings::Keys &keys) const {
    if (row.action >= 0) return keys[KeyBindings::actions()[row.action].action];
    if (row.mod >= 0) return keys.mod(m_ModActions[row.mod].id);
    return -1;
}

void Window::set_key(const KeyRow &row, int key) {
    if (row.action >= 0) m_Keys[KeyBindings::actions()[row.action].action] = key;
    else if (row.mod >= 0) m_Keys.mods[m_ModActions[row.mod].id] = key;
}

void Window::close_keys() {
    m_KeysPage = false;
    m_Capturing = false;
}

bool Window::key_select(int row) {
    if (!key_selectable(row) || row == m_KeySelected) return false;
    m_KeySelected = row;
    m_Capturing = false;
    return true;
}

void Window::key_move(int delta) {
    const int count = key_row_count();
    int row = m_KeySelected;
    const int step = delta > 0 ? 1 : -1;
    for (int n = delta > 0 ? delta : -delta; n > 0; --n) {
        do row = (row + step + count) % count;
        while (!key_selectable(row));
    }
    m_KeySelected = row;
    m_Capturing = false;
    // Scrolled to it, with its group's row when that is just above it.
    const int first = key_group(row - 1) ? row - 1 : row;
    if (first < m_KeyTop) m_KeyTop = first;
    if (row >= m_KeyTop + key_rows_shown) m_KeyTop = row - key_rows_shown + 1;
}

void Window::key_scroll(int rows) {
    m_KeyTop = std::clamp(m_KeyTop + rows, 0, std::max(0, key_row_count() - key_rows_shown));
}

void Window::key_activate() {
    const KeyRow &row = m_KeyRows[m_KeySelected];
    if (row.action >= 0 || row.mod >= 0) {
        m_Capturing = true;
        return;
    }
    if (row.group < 0 && !row.blank) m_Keys = m_KeysBase;  // the last row
}

void Window::key_press(int vk) {
    if (!m_Capturing) return;
    m_Capturing = false;
    // Esc cancels; Tab and F9 are the battle's own (the statistics, a screenshot), before any keys of it.
    if (vk == 0x1B || vk == 0x09 || vk == 0x78 || vk <= 0 || vk >= 256) return;
    set_key(m_KeyRows[m_KeySelected], vk);
}

void Window::key_default() {
    const KeyRow &row = m_KeyRows[m_KeySelected];
    set_key(row, key_of(row, m_KeysBase));
}

std::wstring Window::key_label(int row) const {
    const KeyRow &r = m_KeyRows[row];
    if (r.action >= 0) {
        const auto &action = KeyBindings::actions()[r.action];
        return m_Language == Language::Russian ? action.russian : action.english;
    }
    if (r.mod >= 0) return m_Language == Language::Russian ? m_ModActions[r.mod].russian : m_ModActions[r.mod].english;
    if (r.group >= 0) return KeyBindings::group_name(KeyBindings::Group(r.group), m_Language == Language::Russian);
    if (r.blank) return {};
    return text(L"All keys as the game's", L"\u0412\u0441\u0435 \u043a\u043b\u0430\u0432\u0438\u0448\u0438 \u043a\u0430\u043a \u0432 \u0438\u0433\u0440\u0435");
}

std::wstring Window::key_value(int row) const {
    const KeyRow &r = m_KeyRows[row];
    if (r.action < 0 && r.mod < 0) return {};
    if (m_Capturing && row == m_KeySelected) return L"...";
    return KeyBindings::shown(key_of(r, m_Keys));
}

bool Window::key_conflict(int row) const {
    const KeyRow &r = m_KeyRows[row];
    if (r.action < 0 && r.mod < 0) return false;
    const int key = key_of(r, m_Keys);
    if (key < 0) return false;
    for (int other = 0; other < key_row_count(); ++other) {
        const KeyRow &o = m_KeyRows[other];
        if (other == row || o.group != r.group || (o.action < 0 && o.mod < 0)) continue;
        // (The game's own pairs, its keys as they were, are its choice: the arrows twice, E.)
        const bool set = key != key_of(r, m_KeysBase) || key_of(o, m_Keys) != key_of(o, m_KeysBase);
        if (key_of(o, m_Keys) == key && set) return true;
    }
    return false;
}

std::wstring Window::keys_title() const { return text(L"Keys", L"\u041a\u043b\u0430\u0432\u0438\u0448\u0438"); }

std::wstring Window::keys_legend() const {
    return m_Capturing ? text(L"Press a key; Esc: cancel", L"\u041d\u0430\u0436\u043c\u0438\u0442\u0435 \u043a\u043b\u0430\u0432\u0438\u0448\u0443; Esc - \u043e\u0442\u043c\u0435\u043d\u0430")
                       : text(L"Enter: set, Del: the game's, Esc: back", L"Enter - \u043d\u0430\u0437\u043d\u0430\u0447\u0438\u0442\u044c, Del - \u043a\u0430\u043a \u0432 \u0438\u0433\u0440\u0435, Esc - \u043d\u0430\u0437\u0430\u0434");
}

int Window::key_hit(int x, int y) const {
    if (y < Layout::first_row_y || x < Layout::label_x - 10 || x >= Layout::cell_right + 10) return -1;
    const int row = m_KeyTop + (y - Layout::first_row_y) / Layout::row_height;
    if (row - m_KeyTop >= key_rows_shown || !key_selectable(row)) return -1;
    return row;
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
    return std::to_wstring(UiTheme::border()) + L"|_BORDER:0:0:0:28|_FONT:Font.Heavy14|_WIDTH:" + std::to_wstring(Layout::width) + L"|_HEIGHT:" +
           std::to_wstring(Layout::content_height) +
           L"|_TEXTP:L|_TEXT: |_MOD:COPY|_BITMAP:bhole2:CL:6|_MOD:COPY|_BITMAP:bhole2:CR:6|_TEXTP:B" + options_text(window);
}

std::wstring startup_template(const Window &window) {
    std::wstring result = std::to_wstring(UiTheme::border()) + L"|_BORDER:0:0:0:0|_FONT:Font.Heavy14|_WIDTH:" + std::to_wstring(Layout::width) + L"|_HEIGHT:" +
                          std::to_wstring(StartupLayout::content_height) + L"|_TEXTP:L|_TEXT: |_TEXTP:B" + options_text(window);
    const bool russian = window.language() == Language::Russian;
    result += text_element(russian ? L"( \u041f\u0440\u0438\u043c\u0435\u043d\u0438\u0442\u044c )" : L"( Apply )",
                           L"Font.Heavy14", selected_color(), StartupLayout::apply_x, StartupLayout::buttons_y,
                           StartupLayout::button_width, 1);
    result += text_element(russian ? L"( \u041e\u0442\u043c\u0435\u043d\u0430 )" : L"( Cancel )", L"Font.Heavy14",
                           selected_color(), StartupLayout::cancel_x, StartupLayout::buttons_y, StartupLayout::button_width, 1);
    return result;
}

int startup_button(int x, int y) {
    if (y < StartupLayout::buttons_y || y >= StartupLayout::buttons_y + Layout::row_height) return 0;
    if (x >= StartupLayout::apply_x && x < StartupLayout::apply_x + StartupLayout::button_width) return 1;
    if (x >= StartupLayout::cancel_x && x < StartupLayout::cancel_x + StartupLayout::button_width) return -1;
    return 0;
}

namespace {
std::wstring keys_text(const Window &window) {
    std::wstring result;
    result += text_element(window.keys_title(), L"Font.2Ranger", text_color(), 0, Layout::title_y, Layout::width, 1);
    const int last = std::min(window.key_row_count(), window.key_top() + Window::key_rows_shown);
    for (int row = window.key_top(); row < last; ++row) {
        const int y = row_top(row - window.key_top());
        if (window.key_label(row).empty()) continue;  // the empty row
        if (window.key_group(row)) {
            result += text_element(window.key_label(row), L"Font.Heavy13", group_color(), Layout::label_x, y,
                                   Layout::previous_x - Layout::label_x, 0);
            continue;
        }
        const bool selected = row == window.key_selected();
        const uint32_t color = selected ? selected_color() : text_color();
        result += text_element(window.key_label(row), L"Font.Heavy14", color, Layout::label_x + option_indent, y,
                               Layout::previous_x - Layout::label_x - option_indent, 0);
        result += text_element(window.key_value(row), L"Font.Heavy14", window.key_conflict(row) ? error_color() : color,
                               Layout::value_x, y, Layout::value_width, 1);
    }
    // Arrows in the ">" place of the first and the last row shown while the wheel or the keys can go on that way.
    if (window.key_more_above())
        result += text_element(up_arrow, L"Font.Arrows14", group_color(), Layout::next_x, row_top(0),
                               Layout::cell_right - Layout::next_x, 0);
    if (window.key_more_below())
        result += text_element(down_arrow, L"Font.Arrows14", group_color(), Layout::next_x,
                               row_top(Window::key_rows_shown - 1), Layout::cell_right - Layout::next_x, 0);
    result += text_element(window.error() ? window.error_text() : window.keys_legend(), L"Font.Heavy13",
                           window.error() ? error_color() : legend_color(), Layout::label_x, Layout::legend_y,
                           Layout::cell_right - Layout::label_x, 0);
    return result;
}

std::wstring options_text(const Window &window) {
    if (window.keys_page()) return keys_text(window);
    std::wstring result;
    result += text_element(window.title(), L"Font.2Ranger", text_color(), 0, Layout::title_y, Layout::width, 1);
    std::vector<int> rows;
    if (window.mods_page()) {
        const auto &order = window.mods_rows();
        const int last = std::min(int(order.size()), window.mods_top() + Window::mods_rows_shown);
        rows.assign(order.begin() + window.mods_top(), order.begin() + last);
    }
    else
        for (int row = 0; row < main_rows; ++row) rows.push_back(row);
    for (const int row : rows) {
        const int y = window.top(row);
        if (is_group(row)) {
            result += text_element(window.label(row), L"Font.Heavy13", group_color(), Layout::label_x, y,
                                   Layout::previous_x - Layout::label_x, 0);
            continue;
        }
        const bool selected = row == window.selected();
        const bool enabled = window.enabled(row);
        // A row that cannot change, or opens a page, has no arrows; nor has the mods page.
        const bool arrowed = enabled && !opener(row) && !window.mods_page();
        const uint32_t color = !enabled                    ? disabled_color()
                               : window.conflicting(row) ? error_color()
                               : selected                ? selected_color()
                                                         : text_color();
        const uint32_t arrows = selected ? selected_color() : arrow_color();
        const int indent = window.setting_row(row) ? 2 * option_indent : option_indent;
        // A mod's row: its "i" left of the value; on or off, a switch in the value's place.
        const bool info = window.mods_page() && window.info_row(row);
        const int label_right = window.mods_page() ? Layout::info_x - 4 : Layout::previous_x;
        result += text_element(window.label(row), L"Font.Heavy14", color, Layout::label_x + indent, y,
                               label_right - Layout::label_x - indent, 0);
        const auto button = [&](int x, const std::wstring &bitmap) {
            result += L"|_POS:" + std::to_wstring(x) + L":" + std::to_wstring(y) + L"|_BITMAP:" + bitmap + L":B";
        };
        if (info) button(Layout::info_x, info_bitmap());
        if (info && window.switch_row(row)) {
            button(Layout::switch_x, switch_bitmap(enabled && window.switched_on(row)));
            continue;
        }
        if (arrowed)
            result += text_element(L"<", L"Font.Heavy14", arrows, Layout::previous_x, y,
                                   Layout::value_x - Layout::previous_x, 0);
        result += text_element(window.value(row), L"Font.Heavy14", color, Layout::value_x, y, Layout::value_width, 1);
        if (arrowed)
            result += text_element(L">", L"Font.Heavy14", arrows, Layout::next_x, y,
                                   Layout::cell_right - Layout::next_x, 0);
    }
    // The mods page scrolled: arrows right of the first and the last row shown while it can go on that way.
    if (window.mods_page() && window.mods_more_above())
        result += text_element(up_arrow, L"Font.Arrows14", group_color(), Layout::cell_right + 4, row_top(0),
                               Layout::width - Layout::cell_right - 8, 0);
    if (window.mods_page() && window.mods_more_below())
        result += text_element(down_arrow, L"Font.Arrows14", group_color(), Layout::cell_right + 4,
                               row_top(Window::mods_rows_shown - 1), Layout::width - Layout::cell_right - 8, 0);
    // A failed save's text in the legend's place (the window has no room for both).
    result += text_element(window.error() ? window.error_text() : window.legend(), L"Font.Heavy13",
                           window.error() ? error_color() : legend_color(), Layout::label_x, Layout::legend_y,
                           Layout::cell_right - Layout::label_x, 0);
    return result;
}
}  // namespace

}  // namespace SettingsModel
