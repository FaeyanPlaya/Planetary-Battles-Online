// MatrixGame - licensed under GPLv2 or any later version.
#include "MapModel.hpp"

#include <algorithm>
#include <cstdint>

namespace MapModel {
namespace {

constexpr uint32_t text_color = 0xFF000000;
constexpr uint32_t current_color = 0xFF3F6C8C;
constexpr uint32_t selected_color = 0xFF0B5FA5;
constexpr uint32_t arrow_color = 0xFF7D8890;
constexpr uint32_t legend_color = 0xFF5E676E;
constexpr uint32_t error_color = 0xFFB01818;

using SettingsModel::text_element;

}  // namespace

Window::Window(std::vector<MapCatalog::Map> maps, const std::wstring &current, Language language,
               const MapMetadata::Catalog &metadata, BattleChoice::Bonus bonus)
  : m_Maps(std::move(maps)), m_Language(language), m_Bonus(bonus) {
    for (const auto &map : m_Maps) {
        std::wstring key = map.name + L".cmap";
        for (auto &ch : key) if (ch >= L'A' && ch <= L'Z') ch += L'a' - L'A';
        const auto found = metadata.find(key);
        m_Metadata.push_back(found == metadata.end() ? MapMetadata::Entry{} : found->second);
    }
    m_Current = MapCatalog::find(m_Maps, current);
    m_Selected = m_Current >= 0 ? m_Current : (m_Maps.empty() ? -1 : 0);
    wrap_selected_description();
}

const MapCatalog::Map *Window::selected_map() const { return m_Selected < 0 ? nullptr : &m_Maps[m_Selected]; }

bool Window::select(int index) {
    if (index < 0 || index >= static_cast<int>(m_Maps.size()) || index == m_Selected) return false;
    m_Selected = index;
    m_DescriptionOffset = 0;
    wrap_selected_description();
    m_Error = Error::None;
    return true;
}

bool Window::move(int delta) {
    if (m_Maps.empty()) return false;
    return select(std::clamp(m_Selected + delta, 0, static_cast<int>(m_Maps.size()) - 1));
}

bool Window::page(int delta) {
    if (m_Maps.empty()) return false;
    const int target = first_visible() + delta * Layout::visible_rows;
    if (target < 0) return select(0);
    if (target >= static_cast<int>(m_Maps.size())) return select(static_cast<int>(m_Maps.size()) - 1);
    return select(target);
}

const wchar_t *Window::text(const wchar_t *english, const wchar_t *russian) const {
    return m_Language == Language::Russian ? russian : english;
}

std::wstring Window::title() const { return text(L"New battle", L"\u041d\u043e\u0432\u0430\u044f \u0431\u0438\u0442\u0432\u0430"); }

std::wstring Window::legend() const {
    return text(L"Arrows: map. 1-3: bonus. PgUp/PgDn: description. Enter: start. Esc: back", L"\u0421\u0442\u0440\u0435\u043b\u043a\u0438: \u043a\u0430\u0440\u0442\u0430. 1-3: \u0431\u043e\u043d\u0443\u0441. PgUp/PgDn: \u0442\u0435\u043a\u0441\u0442. Enter: \u0441\u0442\u0430\u0440\u0442. Esc: \u043d\u0430\u0437\u0430\u0434");
}

std::wstring Window::pager() const {
    if (m_Maps.empty()) return {};
    return std::to_wstring(first_visible() / Layout::visible_rows + 1) + L"/" + std::to_wstring(page_count(*this));
}

std::wstring Window::caption() const {
    const auto *map = selected_map();
    if (!map) return {};
    return map->loose ? display_name(m_Selected) + text(L" (loose file)", L" (\u043e\u0442\u0434\u0435\u043b\u044c\u043d\u044b\u0439 \u0444\u0430\u0439\u043b)") : display_name(m_Selected);
}

std::wstring Window::missing_preview() const { return text(L"No preview", L"\u041d\u0435\u0442 \u0438\u0437\u043e\u0431\u0440\u0430\u0436\u0435\u043d\u0438\u044f"); }

std::wstring Window::empty_text() const { return text(L"No maps found", L"\u041a\u0430\u0440\u0442\u044b \u043d\u0435 \u043d\u0430\u0439\u0434\u0435\u043d\u044b"); }

std::wstring Window::error_text() const {
    switch (m_Error) {
        case Error::Unreadable:
            return text(L"This map cannot be loaded; choose another",
                        L"\u042d\u0442\u0443 \u043a\u0430\u0440\u0442\u0443 \u043d\u0435 \u0443\u0434\u0430\u043b\u043e\u0441\u044c \u0437\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044c, \u0432\u044b\u0431\u0435\u0440\u0438\u0442\u0435 \u0434\u0440\u0443\u0433\u0443\u044e");
        case Error::Save:
            return text(L"The choice could not be saved; see test.log",
                        L"\u041d\u0435 \u0443\u0434\u0430\u043b\u043e\u0441\u044c \u0441\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c \u0432\u044b\u0431\u043e\u0440, \u0441\u043c. test.log");
        case Error::None: break;
    }
    return {};
}

const MapMetadata::Entry *Window::metadata() const {
    return m_Selected < 0 ? nullptr : &m_Metadata[m_Selected];
}

std::wstring Window::display_name(int index) const {
    if (index < 0 || index >= static_cast<int>(m_Maps.size())) return {};
    return m_Metadata[index].name.empty() ? m_Maps[index].name : MapMetadata::plain_text(m_Metadata[index].name);
}

std::wstring Window::characteristics() const {
    const auto *entry = metadata();
    if (!entry) return {};
    const wchar_t *groups[] = {L"Original", L"Addon", L"Custom"};
    const wchar_t *lengths[] = {L"Small", L"Below average", L"Average", L"Above average", L"Long"};
    const wchar_t *ru_groups[] = {L"\u041e\u0440\u0438\u0433\u0438\u043d\u0430\u043b", L"\u0414\u043e\u043f\u043e\u043b\u043d\u0435\u043d\u0438\u0435", L"\u041f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u0442\u0435\u043b\u044c\u0441\u043a\u0430\u044f"};
    const wchar_t *ru_lengths[] = {L"\u041a\u043e\u0440\u043e\u0442\u043a\u0430\u044f", L"\u041d\u0438\u0436\u0435 \u0441\u0440\u0435\u0434\u043d\u0435\u0439", L"\u0421\u0440\u0435\u0434\u043d\u044f\u044f", L"\u0412\u044b\u0448\u0435 \u0441\u0440\u0435\u0434\u043d\u0435\u0439", L"\u0414\u043e\u043b\u0433\u0430\u044f"};
    const auto group = entry->group >= 0 && entry->group < 3 ? text(groups[entry->group], ru_groups[entry->group]) : text(L"Unclassified", L"\u0411\u0435\u0437 \u043a\u0430\u0442\u0435\u0433\u043e\u0440\u0438\u0438");
    const auto length = entry->length >= 0 && entry->length < 5 ? text(lengths[entry->length], ru_lengths[entry->length]) : text(L"Unknown", L"\u041d\u0435\u0438\u0437\u0432\u0435\u0441\u0442\u043d\u043e");
    return std::wstring(group) + L" / " + length;
}

std::wstring Window::factions() const {
    const auto *entry = metadata();
    return text(L"Enemies: ", L"\u041f\u0440\u043e\u0442\u0438\u0432\u043d\u0438\u043a\u0438: ") + (entry && !entry->sides.empty() ? MapMetadata::plain_text(entry->sides) : text(L"Unknown", L"\u041d\u0435\u0438\u0437\u0432\u0435\u0441\u0442\u043d\u043e"));
}

std::vector<std::wstring> wrap_description(const std::wstring &text, size_t columns) {
    return wrap_description(text, std::max<size_t>(1, columns), [](std::wstring_view line) { return line.size(); });
}

std::vector<std::wstring> wrap_description(const std::wstring &text, size_t width,
                                          const std::function<size_t(std::wstring_view)> &measure) {
    std::vector<std::wstring> lines;
    size_t begin = 0;
    while (begin < text.size()) {
        const auto newline = text.find(L'\n', begin);
        const size_t paragraph_end = newline == std::wstring::npos ? text.size() : newline;
        size_t end = begin;
        while (end < paragraph_end && measure(std::wstring_view(text).substr(begin, end - begin + 1)) <= width) ++end;
        if (end < paragraph_end) {
            // At least one code point makes progress, even for a pathological width.
            if (end == begin) ++end;
            const auto space = text.rfind(L' ', end);
            if (space != std::wstring::npos && space > begin) end = space;
        }
        lines.push_back(text.substr(begin, end - begin));
        begin = end;
        if (begin < text.size() && (text[begin] == L' ' || text[begin] == L'\n')) ++begin;
    }
    return lines;
}

void Window::set_text_measure(std::function<size_t(std::wstring_view)> measure) {
    m_TextMeasure = std::move(measure);
    wrap_selected_description();
}

void Window::wrap_selected_description() {
    const auto *entry = metadata();
    const auto source = entry && !entry->description.empty() ? MapMetadata::plain_text(entry->description)
                         : std::wstring(text(L"No description available.", L"\u041e\u043f\u0438\u0441\u0430\u043d\u0438\u0435 \u043e\u0442\u0441\u0443\u0442\u0441\u0442\u0432\u0443\u0435\u0442."));
    m_Description = m_TextMeasure ? wrap_description(source, Layout::detail_width, m_TextMeasure) : wrap_description(source);
    m_DescriptionOffset = 0;
}

bool Window::scroll_description(int delta) {
    const int end = std::max(0, static_cast<int>(description().size()) - Layout::description_rows);
    const int next = static_cast<int>(std::clamp(static_cast<long long>(m_DescriptionOffset) + delta, 0LL, static_cast<long long>(end)));
    if (next == m_DescriptionOffset) return false;
    m_DescriptionOffset = next;
    return true;
}

bool Window::select_bonus(int index) {
    if (index < 0 || index > 2 || index == static_cast<int>(m_Bonus)) return false;
    m_Bonus = static_cast<BattleChoice::Bonus>(index);
    return true;
}

std::wstring Window::bonus_label(int index) const {
    const wchar_t *english[] = {L"None", L"Air support", L"Armor"};
    const wchar_t *russian[] = {L"\u0411\u0435\u0437 \u0431\u043e\u043d\u0443\u0441\u0430", L"\u0410\u0432\u0438\u0430\u0446\u0438\u044f", L"\u0411\u0440\u043e\u043d\u044f"};
    return text(english[index], russian[index]);
}
std::wstring Window::start_label() const { return text(L"( Start )", L"( \u041d\u0430\u0447\u0430\u0442\u044c )"); }
std::wstring Window::cancel_label() const { return text(L"( Cancel )", L"( \u041e\u0442\u043c\u0435\u043d\u0430 )"); }

std::wstring Window::bonus_effect() const {
    switch (m_Bonus) {
        case BattleChoice::Bonus::AirSupport: return text(L"Wait: 33%", L"\u041e\u0436\u0438\u0434\u0430\u043d\u0438\u0435: 33%");
        case BattleChoice::Bonus::ReinforcedArmor: return text(L"Enemy damage: 50%", L"\u0423\u0440\u043e\u043d \u0432\u0440\u0430\u0433\u0430: 50%");
        default: return text(L"No modifiers", L"\u0411\u0435\u0437 \u0438\u0437\u043c\u0435\u043d\u0435\u043d\u0438\u0439");
    }
}

int page_count(const Window &window) {
    const int count = static_cast<int>(window.maps().size());
    return std::max(1, (count + Layout::visible_rows - 1) / Layout::visible_rows);
}

Hit hit(const Window &window, int x, int y) {
    if (y >= Layout::buttons_y && y < Layout::buttons_y + Layout::row_height) {
        if (x >= 130 && x < 270) return {.action = 1};
        if (x >= 310 && x < 450) return {.action = -1};
    }
    if (x >= Layout::bonus_x && x < Layout::width - 24 && y >= Layout::bonus_y && y < Layout::bonus_y + 60)
        return {.bonus = (y - Layout::bonus_y) / Layout::row_height};
    if (y >= Layout::description_scroll_y && y < Layout::description_scroll_y + 20) {
        if (x >= Layout::preview_x && x < Layout::preview_x + 40) return {.scroll = -4};
        if (x >= Layout::width - 64 && x < Layout::width - 24) return {.scroll = 4};
    }
    if (y >= Layout::pager_y && y < Layout::pager_y + Layout::row_height) {
        if (x >= Layout::previous_x && x < Layout::previous_x + Layout::pager_width) return {.page = -1};
        if (x >= Layout::next_x && x < Layout::next_x + Layout::pager_width) return {.page = 1};
        return {};
    }
    if (x < Layout::list_x || x >= Layout::list_x + Layout::list_width || y < Layout::first_row_y) return {};
    const int row = (y - Layout::first_row_y) / Layout::row_height;
    const int index = window.first_visible() + row;
    if (row >= Layout::visible_rows || index >= static_cast<int>(window.maps().size())) return {};
    return {.index = index};
}

SettingsModel::Resolution preview_size(int width, int height) {
    return preview_size(width, height, {Layout::preview_width, Layout::preview_height});
}

SettingsModel::Resolution preview_size(int width, int height, SettingsModel::Resolution box) {
    if (width <= 0 || height <= 0 || box.width <= 0 || box.height <= 0) return {0, 0};
    // Scale by the more limiting side; smaller images are enlarged to the box as well.
    if (static_cast<long long>(width) * box.height >= static_cast<long long>(height) * box.width)
        return {box.width, std::max(1, static_cast<int>(static_cast<long long>(height) * box.width / width))};
    return {std::max(1, static_cast<int>(static_cast<long long>(width) * box.height / height)), box.height};
}

std::wstring hint_template(const Window &window, SettingsModel::Resolution preview) {
    // Reuse the settings frame with a blank sizing block; actions are text hit areas.
    std::wstring result = L"2|_BORDER:0:0:0:0|_FONT:Font.2Normal|_WIDTH:" + std::to_wstring(Layout::width) +
                          L"|_HEIGHT:" + std::to_wstring(Layout::content_height) +
                          L"|_TEXTP:L|_TEXT: |_TEXTP:B";
    result += text_element(window.title(), L"Font.2Ranger", text_color, 0, Layout::title_y, Layout::width, 1);
    const auto &maps = window.maps();
    if (maps.empty()) {
        result += text_element(window.empty_text(), L"Font.2Normal", text_color, Layout::list_x, Layout::first_row_y,
                               Layout::list_width, 0);
    }
    const int first = window.first_visible();
    for (int row = 0; row < Layout::visible_rows && first + row < static_cast<int>(maps.size()); ++row) {
        const int index = first + row;
        const uint32_t color = index == window.selected() ? selected_color
                               : window.is_current(index) ? current_color
                                                          : text_color;
        result += text_element((index == window.selected() ? L"> " : L"  ") + window.display_name(index), L"Font.2Normal", color, Layout::list_x,
                               Layout::first_row_y + row * Layout::row_height, Layout::list_width, 0);
    }
    if (page_count(window) > 1) {
        result += text_element(L"<", L"Font.2Normal", arrow_color, Layout::previous_x, Layout::pager_y,
                               Layout::pager_width, 0);
        result += text_element(window.pager(), L"Font.2Small", legend_color, Layout::previous_x + Layout::pager_width,
                               Layout::pager_y, Layout::list_width - 2 * Layout::pager_width, 1);
        result += text_element(L">", L"Font.2Normal", arrow_color, Layout::next_x, Layout::pager_y,
                               Layout::pager_width, 0);
    }
    if (window.selected_map()) {
        if (preview.width > 0) {
            result += L"|_POS:" + std::to_wstring(Layout::preview_x + (Layout::preview_width - preview.width) / 2) +
                      L":" + std::to_wstring(Layout::preview_y + (Layout::preview_height - preview.height) / 2) +
                      L"|_BITMAP:" + preview_bitmap + L":B";
        }
        else {
            result += text_element(window.missing_preview(), L"Font.2Small", legend_color, Layout::preview_x,
                                   Layout::preview_y + Layout::preview_height / 2 - Layout::row_height / 2,
                                   Layout::preview_width, 1);
        }
        result += text_element(window.caption(), L"Font.2Normal", selected_color, Layout::preview_x,
                               Layout::caption_y, Layout::detail_width, 0);
        result += text_element(window.characteristics(), L"Font.2Small", text_color, Layout::preview_x, 204, Layout::detail_width, 0);
        result += text_element(window.factions(), L"Font.2Small", text_color, Layout::preview_x, 224, Layout::detail_width, 0);
        const auto &lines = window.description();
        for (int row = 0; row < Layout::description_rows && window.description_offset() + row < static_cast<int>(lines.size()); ++row)
            result += text_element(lines[window.description_offset() + row], L"Font.2Small", text_color,
                                   Layout::preview_x, Layout::description_y + row * 19, Layout::detail_width, 0);
        if (lines.size() > Layout::description_rows) {
            result += text_element(L"^", L"Font.2Normal", selected_color, Layout::preview_x, Layout::description_scroll_y, 40, 1);
            result += text_element(std::to_wstring(window.description_offset() + 1) + L"-" +
                                   std::to_wstring(std::min(static_cast<int>(lines.size()), window.description_offset() + Layout::description_rows)) + L" / " + std::to_wstring(lines.size()),
                                   L"Font.2Small", legend_color, Layout::preview_x + 40, Layout::description_scroll_y, 200, 1);
            result += text_element(L"v", L"Font.2Normal", selected_color, Layout::width - 64, Layout::description_scroll_y, 40, 1);
        }
    }
    for (int i = 0; i < 3; ++i)
        result += text_element((i == static_cast<int>(window.bonus()) ? L"(*) " : L"( ) ") + window.bonus_label(i),
                               L"Font.2Small", selected_color, Layout::bonus_x, Layout::bonus_y + i * 20, 130, 0);
    result += text_element(window.bonus_effect(), L"Font.2Small", text_color, Layout::bonus_x, 145, 130, 0);
    result += text_element(window.start_label(), L"Font.2Normal", selected_color, 130, Layout::buttons_y, 140, 1);
    result += text_element(window.cancel_label(), L"Font.2Normal", selected_color, 310, Layout::buttons_y, 140, 1);
    if (window.error() == Error::None) result += text_element(window.legend(), L"Font.2Small", legend_color, Layout::list_x, Layout::legend_y,
                           Layout::width - Layout::list_x - 24, 0);
    if (window.error() != Error::None) {
        result += text_element(window.error_text(), L"Font.2Small", error_color, Layout::list_x,
                               Layout::error_y,
                               Layout::width - Layout::list_x - 24, 0);
    }
    return result;
}

}  // namespace MapModel
