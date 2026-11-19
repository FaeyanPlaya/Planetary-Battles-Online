// MatrixGame - licensed under GPLv2 or any later version.
#include "PackMenu.hpp"

#include "GameLanguage.hpp"

#include <windows.h>

#include <algorithm>

namespace PackMenu {

namespace {
int index_of(Side side) { return side == Side::Available ? 0 : 1; }
}  // namespace

Menu::Menu(std::vector<ResourcePacks::Pack> packs, ResourcePacks::Choice applied, ResourcePacks::Choice started,
           bool russian)
  : m_Packs(std::move(packs)), m_Applied(std::move(applied)), m_Started(std::move(started)), m_Russian(russian) {
    open();
}

int Menu::find(const std::wstring &id) const {
    for (int i = 0; i < int(m_Packs.size()); ++i)
        if (m_Packs[i].id == id) return i;
    return -1;
}

std::vector<int> Menu::list(Side side) const {
    std::vector<int> result;
    if (side == Side::Chosen) {
        for (const auto &id : m_Pending)
            if (const int i = find(id); i >= 0) result.push_back(i);
        return result;
    }
    for (int i = 0; i < int(m_Packs.size()); ++i)
        if (std::ranges::find(m_Pending, m_Packs[i].id) == m_Pending.end()) result.push_back(i);
    return result;
}

int Menu::focused_pack() const {
    const auto shown = list(m_Side);
    return m_Focus >= 0 && m_Focus < int(shown.size()) ? shown[m_Focus] : -1;
}

void Menu::open() {
    m_Pending = m_Applied;
    m_Top[0] = m_Top[1] = 0;
    m_Hover = Button::None;
    m_IconHover = false;
    m_Side = list(Side::Chosen).empty() ? Side::Available : Side::Chosen;
    m_Focus = list(m_Side).empty() ? -1 : 0;
}

void Menu::set_packs(std::vector<ResourcePacks::Pack> packs) {
    const int focused = focused_pack();
    const std::wstring id = focused >= 0 ? m_Packs[focused].id : std::wstring();
    m_Packs = std::move(packs);
    const auto shown = list(m_Side);
    const int again = id.empty() ? -1 : find(id);
    const auto at = std::ranges::find(shown, again);
    m_Focus = at != shown.end() ? int(at - shown.begin()) : -1;
    fix_focus();
    show_focus();
}

void Menu::fix_focus() {
    const int count = int(list(m_Side).size());
    if (count == 0) {
        const Side other = m_Side == Side::Available ? Side::Chosen : Side::Available;
        if (!list(other).empty()) {
            m_Side = other;
            m_Focus = std::clamp(m_Focus, 0, int(list(other).size()) - 1);
        }
        else m_Focus = -1;
        return;
    }
    m_Focus = std::clamp(m_Focus, 0, count - 1);
}

void Menu::show_focus() {
    for (const Side side : {Side::Available, Side::Chosen}) {
        int &top = m_Top[index_of(side)];
        top = std::clamp(top, 0, std::max(0, int(list(side).size()) - rows()));
    }
    if (m_Focus < 0) return;
    int &top = m_Top[index_of(m_Side)];
    if (m_Focus < top) top = m_Focus;
    if (m_Focus >= top + rows()) top = m_Focus - rows() + 1;
}

void Menu::move(int delta) {
    const int count = int(list(m_Side).size());
    if (count == 0) return;
    m_Focus = m_Focus < 0 ? 0 : std::clamp(m_Focus + delta, 0, count - 1);
    show_focus();
}

void Menu::switch_side(Side side) {
    if (side == m_Side || list(side).empty()) return;
    // As near the same height as the other list has.
    const int row = m_Focus >= 0 ? m_Focus - top(m_Side) : 0;
    m_Side = side;
    m_Focus = std::clamp(top(side) + row, 0, int(list(side).size()) - 1);
    show_focus();
}

void Menu::focus_on(Side side, int place) {
    if (place < 0 || place >= int(list(side).size())) return;
    m_Side = side;
    m_Focus = place;
    show_focus();
}

void Menu::scroll(Side side, int rows_by) {
    int &top = m_Top[index_of(side)];
    top = std::clamp(top + rows_by, 0, std::max(0, int(list(side).size()) - rows()));
}

bool Menu::turn() {
    const int pack = focused_pack();
    if (pack < 0) return false;
    const auto &id = m_Packs[pack].id;
    if (m_Side == Side::Available) {
        if (!m_Packs[pack].problem.empty()) return false;  // a broken pack is not chosen
        m_Pending.insert(m_Pending.begin(), id);
        m_Side = Side::Chosen;
        m_Focus = 0;
    }
    else {
        std::erase(m_Pending, id);
        m_Side = Side::Available;
        const auto shown = list(Side::Available);
        m_Focus = int(std::ranges::find(shown, pack) - shown.begin());
    }
    m_IconHover = false;
    show_focus();
    return true;
}

bool Menu::raise(int delta) {
    if (m_Side != Side::Chosen || m_Focus < 0) return false;
    const auto shown = list(Side::Chosen);
    const int to = m_Focus + delta;
    if (to < 0 || to >= int(shown.size())) return false;
    // The places in m_Pending of the two packs shown (a pack chosen but not installed keeps its place).
    const auto a = std::ranges::find(m_Pending, m_Packs[shown[m_Focus]].id);
    const auto b = std::ranges::find(m_Pending, m_Packs[shown[to]].id);
    std::iter_swap(a, b);
    m_Focus = to;
    show_focus();
    return true;
}

void Menu::set_extra_rows(int rows_more) {
    m_Extra = std::max(0, rows_more);
    show_focus();
}

std::vector<ButtonPlace> Menu::buttons() const {
    constexpr int l = Layout::buttons_left, r = Layout::buttons_right, g = Layout::button_gap;
    constexpr int third = (r - l - 2 * g) / 3;
    const int a = buttons_y(), b = buttons_y() + Layout::button_step;
    return {{Button::Up, l, a, third},
            {Button::Turn, l + third + g, a, third},
            {Button::Down, r - third, a, third},
            {Button::Folder, l, b, third},
            {Button::Cancel, l + third + g, b, third},
            {Button::Done, r - third, b, third}};
}

bool Menu::enabled(Button button) const {
    switch (button) {
        case Button::Up: return m_Side == Side::Chosen && m_Focus > 0;
        case Button::Down: return m_Side == Side::Chosen && m_Focus >= 0 && m_Focus + 1 < int(list(Side::Chosen).size());
        case Button::Turn: {
            const int pack = focused_pack();
            return pack >= 0 && (m_Side == Side::Chosen || m_Packs[pack].problem.empty());
        }
        case Button::None: return false;
        default: return true;
    }
}

Button Menu::button_of_key(int vk) {
    switch (vk) {
        case 'F': return Button::Folder;
        case 'A': return Button::Turn;
        case VK_PRIOR: return Button::Up;
        case VK_NEXT: return Button::Down;
        default: return Button::None;
    }
}

After Menu::press(Button button) {
    if (!enabled(button)) return After::Stay;
    switch (button) {
        case Button::Up: raise(-1); return After::Stay;
        case Button::Down: raise(1); return After::Stay;
        case Button::Turn: turn(); return After::Stay;
        case Button::Done:
            m_Applied = m_Pending;
            return After::MainPage;
        case Button::Cancel:
            m_Pending = m_Applied;
            return After::MainPage;
        default: return After::Stay;
    }
}

bool Menu::set_hover(Button button) {
    if (button == m_Hover) return false;
    m_Hover = button;
    return true;
}

bool Menu::set_icon_hover(Side side, int place) {
    const bool on = place >= 0;
    if (on == m_IconHover && (!on || (side == m_HoverSide && place == m_HoverPlace))) return false;
    m_IconHover = on;
    m_HoverSide = side;
    m_HoverPlace = place;
    return true;
}

Hit Menu::hit(int x, int y) const {
    for (const auto &place : buttons())
        if (x >= place.x && x < place.x + place.width && y >= place.y && y < place.y + Layout::button_height)
            return {Hit::Kind::Button, Side::Available, -1, place.button};
    if (y < Layout::list_y) return {};
    const int row = (y - Layout::list_y) / Layout::row_height;
    if (row >= rows()) return {};
    for (const Side side : {Side::Available, Side::Chosen}) {
        const int left = column_x(side);
        if (x < left || x >= left + Layout::column_width) continue;
        const int place = top(side) + row;
        if (place >= int(list(side).size())) return {};
        const int in_row = y - row_y(row);
        const bool icon = x < left + 2 + Layout::icon && in_row >= 2 && in_row < 2 + Layout::icon;
        return {icon ? Hit::Kind::Icon : Hit::Kind::Pack, side, place};
    }
    return {};
}

std::wstring Menu::title() const { return text(L"Resource packs", L"Наборы ресурсов"); }

std::wstring Menu::header(Side side) const {
    return side == Side::Available ? text(L"Available", L"Доступные") : text(L"Chosen", L"Выбранные");
}

std::wstring Menu::name(int pack) const {
    if (pack < 0 || pack >= int(m_Packs.size())) return {};
    return GameLanguage::pick(m_Russian, m_Packs[pack].name, m_Packs[pack].name_en);
}

std::wstring Menu::description(int pack) const {
    if (pack < 0 || pack >= int(m_Packs.size())) return {};
    const auto &shown = m_Packs[pack];
    if (!shown.problem.empty()) return shown.problem;
    return GameLanguage::pick(m_Russian, shown.description, shown.description_en);
}

std::wstring Menu::button_text(Button button) const {
    switch (button) {
        case Button::Up: return L"(PgUp)";
        case Button::Down: return L"(PgDn)";
        case Button::Folder: return text(L"Packs folder (F)", L"Папка с наборами (F)");
        case Button::Cancel: return text(L"Cancel (Esc)", L"Отмена (Esc)");
        case Button::Turn:
            return m_Side == Side::Chosen ? text(L"Disable (A)", L"Выключить (A)") : text(L"Enable (A)", L"Включить (A)");
        case Button::Done: return text(L"Done (Enter)", L"Готово (Enter)");
        default: return {};
    }
}

std::wstring Menu::legend(bool &error) const {
    error = false;
    if (m_Packs.empty())
        return text(L"No packs in the resourcepacks folder beside the game",
                    L"В папке resourcepacks рядом с игрой нет наборов");
    const int pack = focused_pack();
    if (pack >= 0 && !m_Packs[pack].problem.empty()) {
        error = true;
        return m_Packs[pack].problem;
    }
    if (pack >= 0 && !m_Packs[pack].author.empty()) return text(L"Author: ", L"Автор: ") + m_Packs[pack].author;
    return {};
}

bool Menu::restart_needed() const {
    ResourcePacks::Choice used;
    for (const int pack : list(Side::Chosen))
        if (m_Packs[pack].problem.empty()) used.push_back(m_Packs[pack].id);
    return used != m_Started;
}

std::wstring Menu::notice() const {
    if (!restart_needed()) return {};
    if (m_InBattle) return text(L"The packs change after the battle", L"Наборы сменятся после боя");
    return text(L"The packs change on Done", L"Наборы сменятся по «Готово»");
}

}  // namespace PackMenu
