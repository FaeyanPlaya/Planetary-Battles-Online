// MatrixGame - licensed under GPLv2 or any later version.
#include "ShaderMenu.hpp"
#include "SettingsModel.hpp"

#include <algorithm>

namespace ShaderMenu {

using ShaderPacks::Item;

std::vector<ButtonPlace> Menu::buttons() const {
    constexpr int l = Layout::buttons_left, r = Layout::buttons_right;
    constexpr int half = (r - l - Layout::gap) / 2;
    constexpr int third = (r - l - 2 * Layout::gap) / 3;
    const int a = buttons_y(), b = buttons_y() + Layout::button_step;
    std::vector<ButtonPlace> result{{Button::Folder, l, a, half},
                                    {settings() ? Button::List : Button::PackSettings, r - half, a, half},
                                    {Button::Cancel, l, b, third},
                                    {Button::Apply, l + third + Layout::gap, b, third},
                                    {Button::Done, r - third, b, third}};
    if (further()) result.push_back({Button::Back, Layout::left, Layout::header_button_y, m_BackWidth});
    if (settings()) result.push_back({Button::Reset, Layout::right - m_ResetWidth, Layout::header_button_y, m_ResetWidth});
    return result;
}

void Menu::set_header_widths(int back, int reset) {
    m_BackWidth = back;
    m_ResetWidth = reset;
}

void Menu::reset() {
    const auto *shown = pack();
    if (!shown) return;
    for (const auto &setting : shown->settings) m_Pending.settings.erase(ShaderPacks::setting_key(*shown, setting));
}

void Menu::set_extra_rows(int rows) {
    m_Extra = std::max(0, rows);
    show_focus();
}

int Menu::shown() const {
    if (!settings()) return list_rows();
    return Layout::settings_rows + m_Extra;
}

int Menu::rows_y() const {
    if (!settings()) return Layout::list_y;
    return Layout::settings_y;
}

Button Menu::button_of_key(int vk) {
    switch (vk) {
        case 'F': return Button::Folder;
        case 'S': return Button::PackSettings;
        case 'L': return Button::List;
        case 'A': return Button::Apply;
        case 'B':
        case 0x08: return Button::Back;  // Backspace too
        case 'D': return Button::Reset;
        default: return Button::None;
    }
}

Menu::Menu(std::vector<ShaderPacks::Pack> packs, ShaderPacks::Choice applied, bool russian)
  : m_Packs(std::move(packs)), m_Applied(applied), m_Pending(std::move(applied)), m_Russian(russian) {
    open();
}

int Menu::find(const std::wstring &id) const {
    for (int i = 0; i < int(m_Packs.size()); ++i)
        if (m_Packs[i].id == id) return i;
    return -1;
}

void Menu::open() {
    m_Pending = m_Applied;
    m_Stack.clear();
    m_Focuses.clear();
    m_Chosen = m_Applied.on ? find(m_Applied.pack) : -1;
    m_Pending.on = m_Chosen >= 0;
    m_Before = m_Chosen;
    m_Focus = m_Chosen;
    m_ListTop = 0;
    m_Hover = Button::None;
    show_focus();
}

void Menu::set_packs(std::vector<ShaderPacks::Pack> packs) {
    const std::wstring chosen = m_Chosen >= 0 ? m_Packs[m_Chosen].id : std::wstring();
    m_Packs = std::move(packs);
    m_Chosen = chosen.empty() ? -1 : find(chosen);
    m_Before = m_Chosen;
    if (m_Chosen < 0) m_Pending.on = false;
    if (m_Chosen < 0 || !m_Packs[m_Chosen].problem.empty()) {
        m_Stack.clear();
        m_Focuses.clear();
    }
    if (settings() && m_Stack.back() >= int(m_Packs[m_Chosen].screens.size())) m_Stack.assign(1, 0);
    m_Focus = m_Chosen;
    m_ListTop = std::clamp(m_ListTop, 0, std::max(0, int(m_Packs.size()) - list_rows()));
    if (settings()) first_focus();
    show_focus();
}

void Menu::toggle() {
    if (m_Packs.empty()) return;
    m_Pending.on = !m_Pending.on;
    m_Focus = -1;
    if (!m_Pending.on) {
        m_Before = m_Chosen;
        m_Chosen = -1;
        return;
    }
    m_Chosen = m_Before >= 0 && m_Before < int(m_Packs.size()) ? m_Before : 0;
    m_Pending.pack = m_Packs[m_Chosen].id;
}

void Menu::choose(int pack) {
    if (pack < 0 || pack >= int(m_Packs.size())) return;
    m_Chosen = m_Focus = m_Before = pack;
    m_Pending.on = true;
    m_Pending.pack = m_Packs[pack].id;
    show_focus();
}

const ShaderPacks::Pack *Menu::pack() const { return m_Chosen >= 0 ? &m_Packs[m_Chosen] : nullptr; }

const ShaderPacks::Screen *Menu::screen() const {
    const auto *shown = pack();
    if (!settings() || !shown || m_Stack.back() >= int(shown->screens.size())) return nullptr;
    return &shown->screens[m_Stack.back()];
}

int Menu::row_count() const { return screen() ? int(screen()->items.size()) : 0; }

Item Menu::item(int row) const { return row >= 0 && row < row_count() ? screen()->items[row] : Item{}; }

bool Menu::focusable(int row) const { return item(row).kind != Item::Kind::Empty; }

bool Menu::focus_row(int row) {
    if (!focusable(row) || row == m_RowFocus) return false;
    m_RowFocus = row;
    return true;
}

void Menu::first_focus() {
    m_RowFocus = 0;
    while (m_RowFocus < row_count() && !focusable(m_RowFocus)) ++m_RowFocus;
    m_RowTop = 0;
}

void Menu::open_screen(int index) {
    m_Stack.push_back(index);
    first_focus();
}

void Menu::show_focus() {
    if (settings()) {
        if (m_RowFocus < m_RowTop) m_RowTop = m_RowFocus;
        if (m_RowFocus >= m_RowTop + shown()) m_RowTop = m_RowFocus - shown() + 1;
        return;
    }
    if (m_Focus < 0) return;
    if (m_Focus < m_ListTop) m_ListTop = m_Focus;
    if (m_Focus >= m_ListTop + list_rows()) m_ListTop = m_Focus - list_rows() + 1;
}

void Menu::move(int delta) {
    if (settings()) {
        const int count = row_count();
        if (count == 0 || std::ranges::none_of(screen()->items, [](const Item &i) { return i.kind != Item::Kind::Empty; }))
            return;
        int row = m_RowFocus;
        do row = (row + delta % count + count) % count;
        while (!focusable(row));
        m_RowFocus = row;
        show_focus();
        return;
    }
    const int count = int(m_Packs.size()) + 1;  // the Shaders row, then the packs
    const int place = (m_Focus + 1 + delta % count + count) % count;
    if (place == 0) m_Focus = -1;
    else choose(place - 1);
}

void Menu::scroll(int rows) {
    int &top = settings() ? m_RowTop : m_ListTop;
    top = std::clamp(top + rows, 0, std::max(0, count() - shown()));
}

bool Menu::step(int row, int direction) {
    const Item at = item(row);
    if (at.kind == Item::Kind::Profile && direction != 0) {
        // Round the profiles; from the person's own settings to the first (or the last, going back).
        const auto &shown = *pack();
        const int count = int(shown.profiles.size());
        if (count == 0) return false;
        const int now = ShaderPacks::profile_now(shown, m_Pending.settings);
        const int next = now < 0 ? (direction > 0 ? 0 : count - 1) : ((now + (direction > 0 ? 1 : -1)) % count + count) % count;
        const auto before = m_Pending.settings;
        ShaderPacks::set_profile(shown, next, m_Pending.settings);
        return m_Pending.settings != before;
    }
    if (at.kind != Item::Kind::Setting || direction == 0) return false;
    const auto &shown = *pack();
    const auto &setting = shown.settings[at.index];
    const auto now = std::ranges::find(setting.values, ShaderPacks::setting_value(shown, setting, m_Pending.settings));
    if (now == setting.values.end()) return false;
    const auto before = m_Pending.settings;
    m_Pending.settings[ShaderPacks::setting_key(shown, setting)] =
            setting.values[Mods::step_index(setting, int(now - setting.values.begin()), direction > 0 ? 1 : -1)];
    return m_Pending.settings != before;
}

bool Menu::can_step(int row, int direction) const {
    const Item at = item(row);
    if (at.kind == Item::Kind::Profile) return !pack()->profiles.empty();
    if (at.kind != Item::Kind::Setting) return false;
    const auto &shown = *pack();
    const auto &setting = shown.settings[at.index];
    const auto now = std::ranges::find(setting.values, ShaderPacks::setting_value(shown, setting, m_Pending.settings));
    if (now == setting.values.end()) return false;
    const int index = int(now - setting.values.begin());
    return Mods::step_index(setting, index, direction) != index;
}

bool Menu::open_row(int row) {
    const Item at = item(row);
    if (at.kind != Item::Kind::Screen || m_Stack.size() >= 16) return false;
    m_Focuses.push_back(m_RowFocus);
    open_screen(at.index);
    return true;
}

bool Menu::enabled(Button button) const {
    switch (button) {
        case Button::PackSettings: return !settings() && m_Chosen >= 0 && m_Packs[m_Chosen].problem.empty();
        case Button::List: return settings();
        case Button::Back: return further();
        case Button::Reset: {
            const auto *shown = pack();
            if (!settings() || !shown) return false;
            return std::ranges::any_of(shown->settings, [&](const Mods::SettingLine &s) {
                return ShaderPacks::setting_value(*shown, s, m_Pending.settings) != s.default_value;
            });
        }
        case Button::Apply: return m_Pending != m_Applied;
        case Button::None: return false;
        default: return true;
    }
}

After Menu::press(Button button) {
    if (!enabled(button)) return After::Stay;
    switch (button) {
        case Button::PackSettings:
            m_Pending.on = true;
            m_Pending.pack = m_Packs[m_Chosen].id;
            apply();
            m_Focuses.clear();
            open_screen(0);
            return After::Stay;
        case Button::Done:
            if (!settings()) {
                m_Pending.on = m_Chosen >= 0;
                if (m_Chosen >= 0) m_Pending.pack = m_Packs[m_Chosen].id;
            }
            apply();
            m_Stack.clear();
            return After::MainPage;
        case Button::Cancel:
            if (!settings()) {
                m_Pending = m_Applied;
                return After::MainPage;
            }
            m_Pending.settings = m_Applied.settings;
            m_Stack.clear();
            m_Focus = m_Chosen;
            return After::Stay;
        case Button::List:
            apply();
            m_Stack.clear();
            m_Focus = m_Chosen;
            show_focus();
            return After::Stay;
        case Button::Apply: apply(); return After::Stay;
        case Button::Back: return escape();  // one screen back, nothing applied
        case Button::Reset: reset(); return After::Stay;
        default: return After::Stay;
    }
}

After Menu::escape() {
    if (further()) {
        m_Stack.pop_back();
        m_RowFocus = m_Focuses.empty() ? 0 : m_Focuses.back();
        if (!m_Focuses.empty()) m_Focuses.pop_back();
        m_RowTop = 0;
        show_focus();
        return After::Stay;
    }
    return press(Button::Cancel);
}

After Menu::enter() {
    if (settings() && open_row(m_RowFocus)) return After::Stay;
    return press(Button::Done);
}

bool Menu::set_hover(Button button) {
    if (button == m_Hover) return false;
    m_Hover = button;
    return true;
}

int Menu::row_y(int place) const {
    return rows_y() + place * Layout::row_height;
}

Hit Menu::hit(int x, int y) const {
    for (const auto &place : buttons())
        if (x >= place.x && x < place.x + place.width && y >= place.y && y < place.y + Layout::button_height)
            return {Hit::Kind::Button, -1, 0, place.button};
    using L = Layout;
    if (!settings()) {
        if (x < Layout::left || x >= Layout::right) return {};
        if (y >= Layout::on_y && y < Layout::on_y + Layout::row_height) return {Hit::Kind::On};
        if (y < rows_y()) return {};
        const int place = (y - rows_y()) / Layout::row_height;
        if (place >= list_rows() || m_ListTop + place >= int(m_Packs.size())) return {};
        return {Hit::Kind::Pack, m_ListTop + place};
    }
    if (y < rows_y() || x < Layout::left || x >= L::cell_right + 10) return {};
    const int place = (y - rows_y()) / Layout::row_height;
    const int row = m_RowTop + place;
    if (place >= shown() || !focusable(row)) return {};
    if (!comment(row).empty() && x >= Layout::info_x && x < Layout::info_x + Layout::info_size) return {Hit::Kind::Info, row};
    if (x < L::previous_x) return {Hit::Kind::Row, row};
    return {Hit::Kind::Row, row, x < L::value_x + L::value_width / 2 ? -1 : 1};
}

std::wstring Menu::pack_name(int index) const {
    if (index < 0 || index >= int(m_Packs.size())) return {};
    const auto &shown = m_Packs[index];
    return GameLanguage::pick(m_Russian, shown.name, shown.name_en);
}

std::wstring Menu::title() const {
    if (!settings()) return text(L"Shader packs", L"Наборы шейдеров");
    const auto *shown = screen();
    if (m_Stack.size() == 1 || !shown) return pack_name(m_Chosen);
    return GameLanguage::pick(m_Russian, shown->russian, shown->english);
}

std::wstring Menu::label(int row) const {
    const Item at = item(row);
    const auto *shown = pack();
    const auto pick = [this](const std::wstring &russian, const std::wstring &english) {
        return GameLanguage::pick(m_Russian, russian, english);
    };
    switch (at.kind) {
        case Item::Kind::Setting: return pick(shown->settings[at.index].russian, shown->settings[at.index].english);
        case Item::Kind::Screen: return pick(shown->screens[at.index].russian, shown->screens[at.index].english);
        case Item::Kind::Note: return pick(shown->notes[at.index].russian, shown->notes[at.index].english);
        case Item::Kind::Profile: return text(L"Profile", L"Профиль");
        default: return {};
    }
}

std::wstring Menu::value(int row) const {
    const Item at = item(row);
    if (at.kind == Item::Kind::Screen) return text(L"Open", L"Открыть");
    if (at.kind == Item::Kind::Profile) {
        const int now = ShaderPacks::profile_now(*pack(), m_Pending.settings);
        if (now < 0) return text(L"Custom", L"Свой");
        return GameLanguage::pick(m_Russian, pack()->profiles[now].russian, pack()->profiles[now].english);
    }
    if (at.kind != Item::Kind::Setting) return {};
    const std::wstring value = ShaderPacks::setting_value(*pack(), pack()->settings[at.index], m_Pending.settings);
    // A switch's words in the person's language (its code still sees On and Off).
    if (value == L"On") return text(L"On", L"Вкл");
    if (value == L"Off") return text(L"Off", L"Выкл");
    return value;
}

std::wstring Menu::comment(int row) const {
    const Item at = item(row);
    const auto *shown = pack();
    std::string id;
    if (at.kind == Item::Kind::Setting) id = shown->settings[at.index].id;
    else if (at.kind == Item::Kind::Note) id = shown->notes[at.index].id;
    else if (at.kind == Item::Kind::Profile) id = ShaderPacks::profile_row;
    else return {};
    const auto found = shown->comments.find(id);
    if (found == shown->comments.end()) return {};
    return GameLanguage::pick(m_Russian, found->second.russian, found->second.english);
}

std::wstring Menu::button_text(Button button) const {
    switch (button) {
        case Button::Folder: return text(L"Packs folder (F)", L"Папка с наборами (F)");
        case Button::PackSettings: return text(L"Pack settings (S)", L"Настройки набора (S)");
        case Button::Done: return text(L"Done (Enter)", L"Готово (Enter)");
        case Button::Cancel: return text(L"Cancel (Esc)", L"Отмена (Esc)");
        case Button::List: return text(L"Shader pack list (L)", L"Список наборов шейдеров (L)");
        case Button::Back: return text(L"< Back (B)", L"< Назад (B)");
        case Button::Reset: return text(L"Reset (D)", L"Сбросить (D)");
        case Button::Apply: return text(L"Apply (A)", L"Применить (A)");
        default: return {};
    }
}

std::wstring Menu::legend(const std::wstring &problem, bool &error) const {
    error = false;
    if (m_Packs.empty())
        return text(L"No packs in the shaders folder beside the game",
                    L"В папке shaders рядом с игрой нет наборов");
    const auto *shown = pack();
    if (!shown) return {};
    error = true;
    if (!shown->problem.empty()) return shown->problem;
    if (m_Applied.on && m_Applied.pack == shown->id && !problem.empty()) return problem;
    error = false;
    if (!shown->author.empty()) return text(L"Author: ", L"Автор: ") + shown->author;
    return {};
}

}  // namespace ShaderMenu
