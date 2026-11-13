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
constexpr std::array languages{GameLanguage::Choice::System, GameLanguage::Choice::Russian, GameLanguage::Choice::English};
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

bool opener(int row) {
    return row == static_cast<int>(Row::Keys) || row == static_cast<int>(Row::Mods) || row == static_cast<int>(Row::Shaders);
}

namespace {
bool g_battle_rules_locked = false;
}

std::vector<int> main_page() {
    std::vector<int> rows;
    for (int row = 0; row < main_rows; ++row) {
        if (row > 0 && is_group(row)) rows.push_back(-1);
        rows.push_back(row);
    }
    return rows;
}

int rows_for_screen(int screen_height) {
    // The frame (21), the battle's OK and Cancel under it (28) or the startup's buttons (34), a little room.
    constexpr int around = Layout::first_row_y + 2 + Layout::row_height + 21 + StartupLayout::buttons_room + 6;
    return std::clamp((screen_height - around) / Layout::row_height, least_rows, tall_rows);
}

void lock_battle_rules(bool locked) { g_battle_rules_locked = locked; }

bool needs_restart(int row) {
    const auto value = static_cast<Row>(row);
    // NewSky: the sky is loaded with the map, so from the next battle; SavedDesigns: a battle loads that many.
    return value == Row::Skybox || value == Row::Language || value == Row::RobotShadows || value == Row::RandomGenerator || value == Row::NewSky ||
           value == Row::SavedDesigns || (value == Row::DelayedDamage && g_battle_rules_locked);
}

int row_top(int place) { return Layout::first_row_y + place * Layout::row_height; }

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
               Mods::SettingValues mod_settings, std::vector<ShaderPacks::Pack> packs, ShaderPacks::Choice shaders)
  : m_Values(values), m_Original(values), m_Resolutions(std::move(resolutions)), m_Language(language),
    m_Selected(static_cast<int>(Row::Resolution)), m_DefaultPersonal(std::move(default_personal)),
    m_Unavailable(std::move(unavailable)), m_Keys(keys), m_KeysOriginal(keys), m_KeysBase(base),
    m_Installed(mods), m_ModSettings(mod_settings), m_ModSettingsOriginal(std::move(mod_settings)),
    m_ShaderMenu(std::move(packs), std::move(shaders), language == Language::Russian) {
    m_Resolutions = resolution_choices(std::move(m_Resolutions), {values.display.width, values.display.height});
    fit_header();
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

void Window::show(int row) {
    const auto &order = page_rows();
    const auto found = std::ranges::find(order, row);
    if (found == order.end()) return;
    int place = int(found - order.begin());
    int &top = m_ModsPage ? m_ModsTop : m_MainTop;
    // Up to a row: its group's too (and the empty row before it).
    if (place < top) top = place >= 1 && is_group(order[place - 1]) && !m_ModsPage ? std::max(0, place - 2) : place;
    if (place >= top + m_Rows) top = place - m_Rows + 1;
}

void Window::scroll(int rows) {
    int &top = m_ModsPage ? m_ModsTop : m_MainTop;
    top = std::clamp(top + rows, 0, std::max(0, int(page_rows().size()) - m_Rows));
}

void Window::set_rows(int rows) {
    m_Rows = std::clamp(rows, 1, tall_rows);
    m_MainTop = m_ModsTop = 0;
    m_KeyTop = 0;
    show(m_Selected);
}

int Window::top(int row) const {
    const auto &order = page_rows();
    const auto found = std::ranges::find(order, row);
    return row_top(int(found - order.begin()) - page_top());
}

Hit Window::hit(int x, int y) const {
    if (y < Layout::first_row_y || x < Layout::label_x - 10 || x >= Layout::cell_right + 10) return {};
    const int place = (y - Layout::first_row_y) / Layout::row_height;
    const auto &order = page_rows();
    if (place >= m_Rows || page_top() + place >= int(order.size())) return {};
    const int row = order[page_top() + place];
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

std::optional<bool> Window::shader_decided(int row) const {
    const auto value = static_cast<Row>(row);
    const char *name = value == Row::StencilShadows     ? "StencilShadows"
                       : value == Row::ProjectedShadows ? "ProjectedShadows"
                                                        : nullptr;
    if (!name) return std::nullopt;
    return ShaderPacks::game_setting(m_ShaderMenu.packs(), m_ShaderMenu.applied(), name);
}

bool Window::enabled(int row) const {
    if (!option(row)) return false;
    if (shader_decided(row)) return false;
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
    const bool on_page = m_ShadersPage ? false
                         : m_ModsPage  ? mods_row(row) || row >= row_count
                                       : row < row_count && !mods_row(row);
    if (!enabled(row) || !on_page || row == m_Selected) return false;
    m_Selected = row;
    show(row);
    return true;
}

void Window::move(int delta) {
    if (mods_page()) {
        const auto &order = mods_rows();
        const int count = int(order.size());
        int place = int(std::ranges::find(order, m_Selected) - order.begin());
        do {
            place = (place + delta + count) % count;
        } while (!enabled(order[place]));
        m_Selected = order[place];
        show(m_Selected);
        return;
    }
    const int first = m_ModsPage ? mods_first : 0;
    const int count = m_ModsPage ? row_count - mods_first : main_rows;
    int row = m_Selected;
    do {
        row = first + (row - first + delta + count) % count;
    } while (!enabled(row));
    m_Selected = row;
    show(row);
}

void Window::close_mods() {
    m_Selected = static_cast<int>(Row::Mods);
    m_ModsPage = false;
    show(m_Selected);
}

void Window::close_shaders() {
    m_Selected = static_cast<int>(Row::Shaders);
    m_ShadersPage = false;
    show(m_Selected);
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
    if (row == static_cast<int>(Row::Shaders)) {
        m_ShadersPage = true;
        m_ShaderMenu.open();
        fit_header();
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
        const auto now = std::ranges::find(setting.values, Mods::setting_value(mod, setting, m_ModSettings));
        if (now == setting.values.end()) return false;
        const int index = Mods::step_index(setting, int(now - setting.values.begin()), direction > 0 ? 1 : -1);
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
        case Row::Language: values.language = cycle(languages, values.language, direction); break;
        case Row::SoundVolume: values.sound_volume = step(values.sound_volume, direction, 10, 0, 100); break;
        case Row::MusicVolume: values.music_volume = step(values.music_volume, direction, 10, 0, 100); break;
        case Row::StencilShadows: values.stencil_shadows = !values.stencil_shadows; break;
        case Row::ProjectedShadows: values.projected_shadows = !values.projected_shadows; break;
        case Row::Skybox: values.skybox = cycle(skyboxes, values.skybox, direction); break;
        case Row::RobotShadows: values.robot_shadows = !values.robot_shadows; break;
        case Row::ShowFps: values.show_fps = !values.show_fps; break;
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

std::wstring Window::fit(const std::wstring &text, const wchar_t *font, int width) const {
    const int room = width - 6;  // the hint's text box is a little narrower than its width
    if (!m_TextWidth || m_TextWidth(text, font) <= room) return text;
    std::wstring cut = text;
    while (!cut.empty() && m_TextWidth(cut + L"...", font) > room) cut.pop_back();
    while (!cut.empty() && cut.back() == L' ') cut.pop_back();
    return cut + L"...";
}

std::vector<std::wstring> Window::lines(const std::wstring &text, const wchar_t *font, int width, int count) const {
    std::vector<std::wstring> result;
    std::wstring rest = text;
    while (!rest.empty() && int(result.size()) < count) {
        if (int(result.size()) == count - 1 || text_width(rest, font) <= width - 6) {
            result.push_back(fit(rest, font, width));
            break;
        }
        // As many words as fit (a word longer than the line: cut there).
        std::size_t end = 0;
        for (std::size_t space = rest.find(L' '); space != std::wstring::npos; space = rest.find(L' ', space + 1)) {
            if (text_width(rest.substr(0, space), font) > width - 6) break;
            end = space;
        }
        if (end == 0) {
            result.push_back(fit(rest, font, width));
            break;
        }
        result.push_back(rest.substr(0, end));
        rest = rest.substr(end + 1);
    }
    return result;
}

int Window::text_width(const std::wstring &text, const wchar_t *font) const {
    return m_TextWidth ? m_TextWidth(text, font) : int(text.size()) * 7;
}

int Window::content_height() const {
    if (m_ShadersPage) return Layout::content_height + m_ShaderMenu.extra_rows() * Layout::row_height;
    return legend_y() + Layout::row_height;  // (a failed save's text in the legend's place)
}

const wchar_t *Window::text(const wchar_t *english, const wchar_t *russian) const {
    return m_Language == Language::Russian ? russian : english;
}

std::wstring Window::title() const {
    if (m_ShadersPage) return m_ShaderMenu.title();
    if (m_ModsPage) return text(L"Mods", L"\u041c\u043e\u0434\u044b");  // Mody
    return text(L"Settings", L"\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438");  // Nastroiki
}

std::wstring Window::label(int row) const {
    if (row >= row_count && option(row)) {
        const FolderRow &place = m_FolderRows[row - row_count];
        const Mods::Mod &mod = m_Mods[place.mod];
        const bool russian = m_Language == Language::Russian;
        if (place.setting < 0) return GameLanguage::pick(russian, mod.name, mod.name_en);
        const Mods::SettingLine &setting = mod.settings[place.setting];
        return GameLanguage::pick(russian, setting.russian, setting.english);
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
        case Row::Language: result = text(L"Language", L"\u042f\u0437\u044b\u043a"); break;
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
        case Row::ShowFps:  // Pokazyvat' FPS
            result = text(L"Show FPS", L"\u041f\u043e\u043a\u0430\u0437\u044b\u0432\u0430\u0442\u044c FPS");
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
        case Row::Shaders: result = text(L"Shaders", L"\u0428\u0435\u0439\u0434\u0435\u0440\u044b"); break;
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
        case Row::Language:  // each language by its own name
            switch (values.language) {
                case GameLanguage::Choice::System: return text(L"As Windows", L"\u041a\u0430\u043a \u0432 \u0441\u0438\u0441\u0442\u0435\u043c\u0435");
                case GameLanguage::Choice::Russian: return L"\u0420\u0443\u0441\u0441\u043a\u0438\u0439";
                case GameLanguage::Choice::English: return L"English";
            }
            return {};
        case Row::SoundVolume: return std::to_wstring(values.sound_volume) + L"%";
        case Row::MusicVolume: return std::to_wstring(values.music_volume) + L"%";
        case Row::StencilShadows: return on_off(shader_decided(row).value_or(values.stencil_shadows));
        case Row::ProjectedShadows: return on_off(shader_decided(row).value_or(values.projected_shadows));
        case Row::Skybox:
            switch (values.skybox) {
                case Skybox::Off: return on_off(false);
                case Skybox::Low: return text(L"Low", L"\u041d\u0438\u0437\u043a\u043e\u0435");
                case Skybox::High: return text(L"High", L"\u0412\u044b\u0441\u043e\u043a\u043e\u0435");
            }
            return {};
        case Row::RobotShadows: return on_off(values.robot_shadows);
        case Row::ShowFps: return on_off(values.show_fps);
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
        case Row::Shaders: {
            const auto &menu = m_ShaderMenu;
            const auto &applied = menu.applied();
            for (int i = 0; applied.on && i < int(menu.packs().size()); ++i)
                if (menu.packs()[i].id == applied.pack) return menu.pack_name(i);
            return on_off(false);
        }
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
    if (m_ShadersPage) {
        bool error = false;
        return m_ShaderMenu.legend(m_ShadersProblem, error);
    }
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
    if (row >= m_KeyTop + m_Rows) m_KeyTop = row - m_Rows + 1;
}

void Window::key_scroll(int rows) {
    m_KeyTop = std::clamp(m_KeyTop + rows, 0, std::max(0, key_row_count() - m_Rows));
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
    if (row - m_KeyTop >= m_Rows || !key_selectable(row)) return -1;
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

int Window::width() const { return m_ShadersPage ? ShaderMenu::Layout::width : Layout::width; }

void Window::fit_header() {
    const auto fitted = [&](ShaderMenu::Button button) {
        return text_width(m_ShaderMenu.button_text(button), L"Font.Heavy14") + 2 * ShaderMenu::Layout::header_button_pad;
    };
    m_ShaderMenu.set_header_widths(fitted(ShaderMenu::Button::Back), fitted(ShaderMenu::Button::Reset));
}

std::wstring hint_template(const Window &window) {
    // The blank block sizes the frame. The copied holes are transparent, so the OK and Cancel hint
    // buttons placed at their positions show through, as in the Exit/Reset/Surrender confirmations.
    // The shaders screens have their own buttons: no holes.
    const std::wstring holes = window.shaders_page() ? L"" : L"|_MOD:COPY|_BITMAP:bhole2:CL:6|_MOD:COPY|_BITMAP:bhole2:CR:6";
    return std::to_wstring(UiTheme::border()) + L"|_BORDER:0:0:0:28|_FONT:Font.Heavy14|_WIDTH:" + std::to_wstring(window.width()) + L"|_HEIGHT:" +
           std::to_wstring(window.content_height()) + L"|_TEXTP:L|_TEXT: " + holes + L"|_TEXTP:B" + options_text(window);
}

std::wstring startup_template(const Window &window) {
    std::wstring result = std::to_wstring(UiTheme::border()) + L"|_BORDER:0:0:0:0|_FONT:Font.Heavy14|_WIDTH:" + std::to_wstring(window.width()) + L"|_HEIGHT:" +
                          std::to_wstring(window.content_height() + StartupLayout::buttons_room) +
                          L"|_TEXTP:L|_TEXT: |_TEXTP:B" + options_text(window);
    if (window.shaders_page()) return result;
    const bool russian = window.language() == Language::Russian;
    const int buttons_y = window.content_height();
    result += text_element(russian ? L"( \u041f\u0440\u0438\u043c\u0435\u043d\u0438\u0442\u044c )" : L"( Apply )",
                           L"Font.Heavy14", selected_color(), StartupLayout::apply_x, buttons_y,
                           StartupLayout::button_width, 1);
    result += text_element(russian ? L"( \u041e\u0442\u043c\u0435\u043d\u0430 )" : L"( Cancel )", L"Font.Heavy14",
                           selected_color(), StartupLayout::cancel_x, buttons_y, StartupLayout::button_width, 1);
    return result;
}

int startup_button(const Window &window, int x, int y) {
    if (window.shaders_page()) return 0;
    const int buttons_y = window.content_height();
    if (y < buttons_y || y >= buttons_y + Layout::row_height) return 0;
    if (x >= StartupLayout::apply_x && x < StartupLayout::apply_x + StartupLayout::button_width) return 1;
    if (x >= StartupLayout::cancel_x && x < StartupLayout::cancel_x + StartupLayout::button_width) return -1;
    return 0;
}

namespace {
std::wstring keys_text(const Window &window) {
    std::wstring result;
    result += text_element(window.keys_title(), L"Font.2Ranger", text_color(), 0, Layout::title_y, Layout::width, 1);
    const int last = std::min(window.key_row_count(), window.key_top() + window.rows_shown());
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
                               row_top(window.rows_shown() - 1), Layout::cell_right - Layout::next_x, 0);
    result += text_element(window.error() ? window.error_text() : window.keys_legend(), L"Font.Heavy13",
                           window.error() ? error_color() : legend_color(), Layout::label_x, window.legend_y(),
                           Layout::cell_right - Layout::label_x, 0);
    return result;
}

uint32_t with_alpha(uint32_t color, uint32_t alpha) { return (color & 0x00FFFFFF) | (alpha << 24); }

std::wstring frame_name(int width, int height, uint32_t border, uint32_t fill) {
    return L"SettingsFrame" + std::to_wstring(width) + L"x" + std::to_wstring(height) + L"_" + hex(border) + hex(fill);
}

// The frames of the shaders screens, where they are drawn: the pack chosen, the buttons.
struct Placed {
    Frame frame;
    int x, y;
};
std::vector<Placed> shader_frames(const Window &window) {
    std::vector<Placed> result;
    const auto add = [&](int x, int y, int width, int height, uint32_t border, uint32_t fill) {
        result.push_back({{frame_name(width, height, border, fill), width, height, border, fill}, x, y});
    };
    using SL = ShaderMenu::Layout;
    const auto &menu = window.shader_menu();
    const int place = menu.chosen() - menu.list_top();
    if (!menu.settings() && menu.chosen() >= 0 && place >= 0 && place < menu.list_rows())
        add(SL::left, menu.row_y(place) - 1, SL::right - SL::left, SL::row_height + 2, selected_color(),
            with_alpha(selected_color(), 0x24));
    for (const auto &button : menu.buttons()) {
        const bool enabled = menu.enabled(button.button), hover = enabled && menu.hover() == button.button;
        const uint32_t border = !enabled ? with_alpha(disabled_color(), 0x90) : hover ? selected_color() : arrow_color();
        const uint32_t fill = !enabled ? 0 : hover ? with_alpha(selected_color(), 0x30) : with_alpha(text_color(), 0x14);
        add(button.x, button.y, button.width, SL::button_height, border, fill);
    }
    return result;
}

std::wstring bitmap_at(int x, int y, const std::wstring &bitmap) {
    return L"|_POS:" + std::to_wstring(x) + L":" + std::to_wstring(y) + L"|_BITMAP:" + bitmap + L":B";
}

std::wstring shaders_text(const Window &window) {
    using SL = ShaderMenu::Layout;
    const auto &menu = window.shader_menu();
    // The list's title on one line (a pack's long name cut; the legend says it whole); a settings screen's in two lines
    // between its Back and Reset.
    std::wstring result;
    if (!menu.settings())
        result += text_element(window.fit(window.title(), L"Font.2Ranger", SL::width - 40), L"Font.2Ranger",
                               text_color(), 0, Layout::title_y, SL::width, 1);
    else {
        const auto lines = window.lines(window.title(), L"Font.Heavy14", menu.title_width(), 2);
        const int first = SL::header_y + (lines.size() == 1 ? SL::row_height / 2 : 0);
        for (std::size_t i = 0; i < lines.size(); ++i)
            result += text_element(lines[i], L"Font.Heavy14", text_color(), menu.title_left(),
                                   first + int(i) * SL::row_height, menu.title_width(), 1);
    }
    const auto frames = shader_frames(window);
    std::size_t next_frame = 0;
    // A text longer than its place is cut; the focused row's (the pack chosen's) then said whole in the legend.
    std::wstring whole;
    const auto fitted = [&](const std::wstring &text, int width, bool focused) {
        std::wstring shown = window.fit(text, L"Font.Heavy14", width);
        if (focused && shown != text) whole = whole.empty() ? text : whole + L": " + text;
        return shown;
    };
    constexpr int text_x = SL::left + 8;  // the packs' names and the settings' labels
    if (!menu.settings()) {
        // Shaders on or off, an empty row, the packs (the one chosen framed).
        const bool none = menu.packs().empty();
        const uint32_t color = none ? disabled_color() : menu.focus() < 0 ? selected_color() : text_color();
        // In the middle: the word, its switch next to it.
        const std::wstring word = menu.russian() ? L"Шейдеры" : L"Shaders";
        const int word_width = window.text_width(word, L"Font.Heavy14");
        const int left = (SL::width - word_width - SL::switch_gap - Layout::button_size) / 2;
        result += text_element(word, L"Font.Heavy14", color, left, SL::on_y, word_width + 6, 0);
        result += bitmap_at(left + word_width + SL::switch_gap, SL::on_y, switch_bitmap(menu.on()));
        if (menu.chosen() >= 0 && menu.chosen() >= menu.list_top() && menu.chosen() < menu.list_top() + menu.list_rows()) {
            const auto &chosen = frames[next_frame++];
            result += bitmap_at(chosen.x, chosen.y, chosen.frame.name);
        }
        for (int place = 0; place < menu.list_rows() && menu.list_top() + place < int(menu.packs().size()); ++place) {
            const int pack = menu.list_top() + place;
            const uint32_t name_color = !menu.packs()[pack].problem.empty() ? disabled_color()
                                        : pack == menu.chosen()              ? selected_color()
                                                                             : text_color();
            const int width = SL::right - 8 - text_x;
            result += text_element(fitted(menu.pack_name(pack), width, pack == menu.chosen()), L"Font.Heavy14", name_color,
                                   text_x, menu.row_y(place), width, 0);
        }
    }
    else {
        for (int place = 0; place < menu.shown() && menu.top() + place < menu.row_count(); ++place) {
            const int row = menu.top() + place;
            const auto item = menu.item(row);
            if (item.kind == ShaderPacks::Item::Kind::Empty) continue;
            const int y = menu.row_y(place);
            const bool focused = row == menu.row_focus();
            const uint32_t color = focused ? selected_color() : text_color();
            const bool info = !menu.comment(row).empty();
            const bool note = item.kind == ShaderPacks::Item::Kind::Note;
            const int label_right = info ? SL::info_x - 4 : note ? SL::cell_right : SL::previous_x;
            result += text_element(fitted(menu.label(row), label_right - text_x, focused), L"Font.Heavy14",
                                   note && !focused ? legend_color() : color, text_x, y, label_right - text_x, 0);
            if (info) result += bitmap_at(SL::info_x, y, info_bitmap());
            if (note) continue;
            const bool setting = item.kind == ShaderPacks::Item::Kind::Setting ||
                                 item.kind == ShaderPacks::Item::Kind::Profile;  // (its < value > too)
            const uint32_t arrows = focused ? selected_color() : arrow_color();
            // A range at its end: that arrow dimmed (it goes no further).
            const uint32_t back = menu.can_step(row, -1) ? arrows : with_alpha(disabled_color(), 0x90);
            const uint32_t on = menu.can_step(row, 1) ? arrows : with_alpha(disabled_color(), 0x90);
            if (setting)
                result += text_element(L"<", L"Font.Heavy14", back, SL::previous_x, y, SL::value_x - SL::previous_x, 0);
            const std::wstring value = menu.value(row);
            const std::wstring shown = window.fit(value, L"Font.Heavy14", SL::value_width);
            if (focused && shown != value) whole = menu.label(row) + L": " + value;
            result += text_element(shown, L"Font.Heavy14", color, SL::value_x, y, SL::value_width, 1);
            if (setting)
                result += text_element(L">", L"Font.Heavy14", on, SL::next_x, y, SL::cell_right - SL::next_x, 0);
        }
        if (menu.row_count() == 0)
            result += text_element(menu.russian() ? L"У набора нет настроек"
                                                  : L"The pack has no settings",
                                   L"Font.Heavy14", legend_color(), 0, menu.rows_y(), SL::width, 1);
    }
    // Scrolled: arrows right of the first and the last row shown while it goes on that way.
    const int first_y = menu.row_y(0), last_y = menu.row_y(menu.shown() - 1);
    if (menu.more_above())
        result += text_element(up_arrow, L"Font.Arrows14", group_color(), SL::cell_right + 4, first_y, SL::width - SL::cell_right - 8, 0);
    if (menu.more_below())
        result += text_element(down_arrow, L"Font.Arrows14", group_color(), SL::cell_right + 4, last_y, SL::width - SL::cell_right - 8, 0);
    bool error = false;
    std::wstring legend = window.error() ? window.error_text() : menu.legend(window.shaders_problem(), error);
    error = error || window.error();
    const uint32_t legend_colour = error ? error_color() : legend_color();
    if (!menu.settings()) {
        if (!error && !whole.empty()) legend = whole;
        result += text_element(window.fit(legend, L"Font.Heavy13", SL::buttons_right - SL::buttons_left), L"Font.Heavy13",
                               legend_colour, SL::buttons_left, menu.legend_y(), SL::buttons_right - SL::buttons_left, 1);
    }
    else {
        // Under the rows: the chosen row's name (with its value when that is cut) in two lines; a problem instead.
        std::wstring name;
        if (menu.focusable(menu.row_focus())) {
            name = menu.label(menu.row_focus());
            const std::wstring value = menu.value(menu.row_focus());
            if (!value.empty() && window.fit(value, L"Font.Heavy14", SL::value_width) != value) name += L": " + value;
        }
        const auto lines = window.lines(error ? legend : name, L"Font.Heavy14", SL::buttons_right - SL::buttons_left, 2);
        for (std::size_t i = 0; i < lines.size(); ++i)
            result += text_element(lines[i], L"Font.Heavy14", error ? error_color() : text_color(), SL::buttons_left,
                                   menu.name_y() + int(i) * SL::row_height, SL::buttons_right - SL::buttons_left, 1);
    }
    // The buttons: under the rows, not scrolled.
    const auto places = menu.buttons();
    for (std::size_t i = 0; i < places.size(); ++i) {
        const auto &frame = frames[next_frame + i];
        const auto &button = places[i];
        result += bitmap_at(frame.x, frame.y, frame.frame.name);
        const bool enabled = menu.enabled(button.button);
        const uint32_t color = !enabled ? disabled_color() : menu.hover() == button.button ? selected_color() : text_color();
        result += text_element(window.fit(menu.button_text(button.button), L"Font.Heavy14", button.width), L"Font.Heavy14",
                               color, button.x, button.y + 2, button.width, 1);  // as much room over the text as under it
    }
    return result;
}

std::wstring options_text(const Window &window) {
    if (window.keys_page()) return keys_text(window);
    if (window.shaders_page()) return shaders_text(window);
    std::wstring result;
    result += text_element(window.title(), L"Font.2Ranger", text_color(), 0, Layout::title_y, Layout::width, 1);
    const auto &order = window.page_rows();
    const int last = std::min(int(order.size()), window.page_top() + window.rows_shown());
    for (int place = window.page_top(); place < last; ++place) {
        const int row = order[place];
        if (row < 0) continue;  // an empty row
        const int y = row_top(place - window.page_top());
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
    // Scrolled: arrows right of the first and the last row shown while it can go on that way.
    if (window.more_above())
        result += text_element(up_arrow, L"Font.Arrows14", group_color(), Layout::cell_right + 4, row_top(0),
                               Layout::width - Layout::cell_right - 8, 0);
    if (window.more_below())
        result += text_element(down_arrow, L"Font.Arrows14", group_color(), Layout::cell_right + 4,
                               row_top(window.rows_shown() - 1), Layout::width - Layout::cell_right - 8, 0);
    // A failed save's text in the legend's place (the window has no room for both).
    result += text_element(window.error() ? window.error_text() : window.legend(), L"Font.Heavy13",
                           window.error() ? error_color() : legend_color(), Layout::label_x, window.legend_y(),
                           Layout::cell_right - Layout::label_x, 0);
    return result;
}
}  // namespace

std::vector<Frame> frames(const Window &window) {
    std::vector<Frame> result;
    if (!window.shaders_page()) return result;
    for (const auto &placed : shader_frames(window))
        if (std::ranges::find(result, placed.frame) == result.end()) result.push_back(placed.frame);
    return result;
}

}  // namespace SettingsModel
