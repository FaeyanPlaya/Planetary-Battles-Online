// MatrixGame - licensed under GPLv2 or any later version.
#include "LobbyModel.hpp"
#include "Factions.hpp"
#include "Lockstep.hpp"
#include "PlayerName.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace LobbyModel {
namespace {

constexpr uint32_t text_color = 0xFF000000;
// Dark enough to read well on the light panel.
constexpr uint32_t selected_color = 0xFF08498A;
constexpr uint32_t disabled_color = 0xFF6F777D;
constexpr uint32_t arrow_color = 0xFF4A555D;
constexpr uint32_t legend_color = 0xFF384046;
constexpr uint32_t ready_color = 0xFF1B6420;
constexpr uint32_t error_color = 0xFF9A0E0E;
constexpr uint32_t slow_color = 0xFF8C5200;  // a ping that wants a longer delay

using SettingsModel::text_element;

const wchar_t *text(Language language, const wchar_t *english, const wchar_t *russian) {
    return language == Language::Russian ? russian : english;
}

std::wstring frame(int width, int height) {
    return std::to_wstring(SettingsModel::window_border) + L"|_BORDER:0:0:0:0|_FONT:Font.2Normal|_WIDTH:" + std::to_wstring(width) + L"|_HEIGHT:" +
           std::to_wstring(height) + L"|_TEXTP:L|_TEXT: |_TEXTP:B";
}

int people(const Screen &s) { return int(s.people.size()); }

int px(const Screen &s, int value) { return LobbyModel::px(s.scale, value); }

// The font drawn at the screen's scale: the menu's Verdana, heavier, the title in the Rangers face (capitals only).
std::wstring sized_font(double scale, const wchar_t *font) {
    const std::wstring_view f = font;
    const bool title = f == L"Font.2Ranger";
    const int base = title ? 12 : f == L"Font.2Mini" ? 12 : f == L"Font.2Small" ? 13 : 14;
    return (title ? L"Font.Title" : L"Font.Heavy") + std::to_wstring(std::max(6, LobbyModel::px(scale, base)));
}
std::wstring sized_font(const Screen &s, const wchar_t *font) { return sized_font(s.scale, font); }

// Lines of `text` broken at its spaces to fit `width` pixels of `font` (the drawing's own breaking also tears off a
// punctuation mark that does not fit: "word\n; next"). Whole without a way to measure (tests).
std::wstring wrapped(const Screen &s, const std::wstring &text, const wchar_t *font, int width) {
    if (!s.text_width) return text;
    std::wstring result, line;
    for (std::size_t at = 0; at <= text.size();) {
        const std::size_t end = std::min(text.find(L' ', at), text.size());
        const std::wstring word = text.substr(at, end - at);
        if (!line.empty() && s.text_width(line + L" " + word, font) > width) {
            result += line + L"\n";
            line = word;
        }
        else line += (line.empty() ? L"" : L" ") + word;
        at = end + 1;
    }
    return result + line;
}

// A text in the grid's units (SettingsModel::text_element at the screen's scale), `height` tall.
std::wstring el(double scale, const std::wstring &text, const wchar_t *font, uint32_t color, int x, int y, int width,
                int align, int height = Layout::row_height - 1) {
    const auto p = [scale](int value) { return LobbyModel::px(scale, value); };
    // The Rangers face has no Yo: a title's is drawn as Ye, as Russian print often does.
    std::wstring shown = text;
    if (std::wstring_view(font) == L"Font.2Ranger")
        for (wchar_t &ch : shown)
            if (ch == L'\u0451') ch = L'\u0435';
            else if (ch == L'\u0401') ch = L'\u0415';
    auto element = text_element(shown, sized_font(scale, font).c_str(), color, p(x), p(y), p(width), align);
    // The settings' box height, whatever it is, becomes this one (a description takes two lines).
    const std::wstring key = L"|_HEIGHT:";
    if (const auto at = element.find(key); at != std::wstring::npos)
        element.replace(at, element.find(L'|', at + 1) - at, key + std::to_wstring(p(height)));
    return element;
}
std::wstring el(const Screen &s, const std::wstring &text, const wchar_t *font, uint32_t color, int x, int y, int width,
                int align, int height = Layout::row_height - 1) {
    return el(s.scale, text, font, color, x, y, width, align, height);
}

std::wstring delay_label(Language l, int delay, int auto_ms) {
    const std::wstring ms = text(l, L" ms", L" \u043c\u0441");
    if (delay > 0) return text(l, L"Delay: ", L"\u0417\u0430\u0434\u0435\u0440\u0436\u043a\u0430: ") + std::to_wstring(delay) + ms;
    if (auto_ms <= 0)  // the pings are being measured
        return text(l, L"Delay: auto (measuring...)", L"\u0417\u0430\u0434\u0435\u0440\u0436\u043a\u0430: \u0430\u0432\u0442\u043e (\u0437\u0430\u043c\u0435\u0440...)");
    return text(l, L"Delay: auto (", L"\u0417\u0430\u0434\u0435\u0440\u0436\u043a\u0430: \u0430\u0432\u0442\u043e (") + std::to_wstring(auto_ms) + ms + L")";
}

// The battle's delay on auto as the host measured it (Lobby::State::auto_delay), in ms; 0: measuring.
int auto_delay_ms(const Screen &s) { return s.auto_delay; }

const MapRow *selected_row(const Screen &s) {
    return s.selected >= 0 && s.selected < int(s.maps.size()) ? &s.maps[s.selected] : nullptr;
}

std::wstring bonus_label(Language l, BattleChoice::Bonus bonus) {
    switch (bonus) {
        case BattleChoice::Bonus::AirSupport: return text(l, L"Air support", L"\u0410\u0432\u0438\u0430\u0446\u0438\u044f");
        case BattleChoice::Bonus::ReinforcedArmor: return text(l, L"Armor", L"\u0411\u0440\u043e\u043d\u044f");
        default: return text(l, L"No bonus", L"\u0411\u0435\u0437 \u0431\u043e\u043d\u0443\u0441\u0430");
    }
}

std::wstring bonus_effect(Language l, BattleChoice::Bonus bonus) {
    switch (bonus) {
        case BattleChoice::Bonus::AirSupport: return text(l, L"wait 33%", L"\u043e\u0436\u0438\u0434\u0430\u043d\u0438\u0435 33%");
        case BattleChoice::Bonus::ReinforcedArmor: return text(l, L"enemy damage 50%", L"\u0443\u0440\u043e\u043d \u0432\u0440\u0430\u0433\u0430 50%");
        default: return {};
    }
}

std::wstring ai_label(Language l, int level) {
    if (level <= 0) return text(l, L"AI resources: as the map says", L"\u0420\u0435\u0441\u0443\u0440\u0441\u044b \u0418\u0418: \u043a\u0430\u043a \u043d\u0430 \u043a\u0430\u0440\u0442\u0435");
    return text(l, L"AI resources: a player's x", L"\u0420\u0435\u0441\u0443\u0440\u0441\u044b \u0418\u0418: \u043a\u0430\u043a \u0443 \u0438\u0433\u0440\u043e\u043a\u0430 x") + std::to_wstring(level);
}

bool parse_number(std::wstring_view text, int most, int &out) {
    if (text.empty() || text.size() > 5) return false;
    out = 0;
    for (wchar_t ch : text) {
        if (ch < L'0' || ch > L'9') return false;
        out = out * 10 + (ch - L'0');
    }
    return out <= most;
}

}  // namespace

int px(double scale, int value) { return int(std::lround(value * scale)); }

double scale_for(int screen_width, int screen_height) {
    // The hint's frame adds a border round the content, and its ornaments over and under it.
    const double k = std::min(double(screen_width - 40) / Layout::width, double(screen_height - 110) / Layout::content_height);
    return std::clamp(std::floor(k * 20) / 20, 1.0, 4.0);
}

std::wstring fit(const Screen &s, const std::wstring &text, const wchar_t *font, int width) {
    constexpr int margin = 6;  // the hint's text box is a little narrower than its width
    const std::wstring drawn = sized_font(s, font);
    const int room = px(s, width - margin);
    if (!s.text_width || s.text_width(text, drawn.c_str()) <= room) return text;
    std::wstring cut = text;
    while (!cut.empty() && s.text_width(cut + L"...", drawn.c_str()) > room) cut.pop_back();
    while (!cut.empty() && cut.back() == L' ') cut.pop_back();
    return cut + L"...";
}

Fitted fit_row(const Screen &s, const std::wstring &text, int width) {
    if (fit(s, text, L"Font.2Normal", width) == text) return {text, L"Font.2Normal"};
    return {fit(s, text, L"Font.2Small", width), L"Font.2Small"};
}

bool offered(const Screen &s, int index) {
    return index >= 0 && index < int(s.maps.size()) && s.maps[index].capacity >= std::max(2, people(s));
}

bool can_start(const Screen &s) {
    return s.host && people(s) >= (s.local ? 1 : 2) && offered(s, s.selected) && map_fine(s) &&
           Lobby::clients_ready({{}, 0, s.people, {}});
}

bool map_fine(const Screen &s) { return s.map_problem.kind == MapProblem::Kind::None; }

bool mods_fine(const Screen &s) {
    return std::none_of(s.mods.begin(), s.mods.end(), [](const ModRow &m) { return m.on && m.problem != ModRow::Problem::None; });
}

int mods_page_count(const Screen &s) {
    constexpr int per_page = Layout::visible_rows / Layout::mod_rows;
    return std::max(1, (int(s.mods.size()) + per_page - 1) / per_page);
}

int mods_page_moved(const Screen &s, int delta) { return std::clamp(s.mods_page + delta, 0, mods_page_count(s) - 1); }

bool sides_coincide(const Screen &s) {
    unsigned chosen = 0;
    for (const Lobby::Person &p : s.people) {
        if (!p.side) continue;
        if (chosen & (1u << p.side)) return true;
        chosen |= 1u << p.side;
    }
    return false;
}

int page_count(const Screen &s) {
    return std::max(1, (int(s.maps.size()) + Layout::visible_rows - 1) / Layout::visible_rows);
}

int page_of(int row) { return std::max(0, row) / Layout::visible_rows; }

namespace {
std::wstring sort_key(const std::wstring &name) {
    std::wstring key = name;
    for (auto &ch : key) {
        if ((ch >= L'A' && ch <= L'Z') || (ch >= L'\u0410' && ch <= L'\u042F')) ch += 32;
        else if (ch == L'\u0401' || ch == L'\u0451') ch = L'\u0435';  // Yo sorts with Ye
        if (ch >= L'a' && ch <= L'z') ch += 0x1000;  // untranslated names after the Russian ones
    }
    return key;
}

int map_at(const Screen &s, int row) { return s.order.empty() ? row : s.order[row]; }
}  // namespace

std::vector<int> map_order(const std::vector<MapRow> &maps, MapSort sort) {
    std::vector<int> order(maps.size());
    std::vector<std::wstring> keys;
    for (std::size_t i = 0; i < maps.size(); ++i) {
        order[i] = int(i);
        keys.push_back(sort_key(maps[i].name));
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        if (sort == MapSort::People) {
            const int ca = maps[a].capacity > 0 ? maps[a].capacity : 100, cb = maps[b].capacity > 0 ? maps[b].capacity : 100;
            if (ca != cb) return ca < cb;
        }
        return keys[a] < keys[b];
    });
    return order;
}

int row_of(const Screen &s, int index) {
    if (index < 0 || index >= int(s.maps.size())) return -1;
    if (s.order.empty()) return index;
    const auto at = std::find(s.order.begin(), s.order.end(), index);
    return at == s.order.end() ? -1 : int(at - s.order.begin());
}

const std::vector<Line> &separators() {
    static const std::vector<Line> lines = {
        {352, Layout::header_y, 440, true},                                       // maps | preview
        {625, Layout::header_y, 440, true},                                       // preview | people
        {Layout::preview_x, 372, Layout::preview_width, false},                   // map info | direct control
        {Layout::people_x, 184, Layout::people_width, false},                     // people | bonus
        {Layout::people_x, 290, Layout::people_width, false},                     // bonus | AI resources, delay
        {Layout::people_x, 352, Layout::people_width, false},                     // delay | address
        {Layout::list_x, 500, Layout::width - 2 * Layout::list_x, false},         // over the status
        {Layout::list_x, 558, Layout::width - 2 * Layout::list_x, false},         // over the buttons
    };
    return lines;
}

std::wstring line_bitmap(int index) { return L"LobbyLine" + std::to_wstring(index); }

std::wstring toggle_bitmap(bool on) { return on ? L"LobbyToggleOn" : L"LobbyToggleOff"; }

Hit hit(const Screen &s, int x, int y) {
    if (y >= Layout::buttons_y && y < Layout::buttons_y + Layout::row_height) {
        if (x >= Layout::primary_x && x < Layout::primary_x + Layout::button_width) return {.action = 1};
        if (x >= Layout::leave_x && x < Layout::leave_x + Layout::button_width) return {.action = -1};
        if (x >= Layout::mods_x && x < Layout::mods_x + Layout::button_width) return {.action = 2};
        return {};
    }
    if (x >= Layout::people_x && x < Layout::people_x + Layout::people_width) {
        // The host's: a client's name asks whether to send them away; that row then answers.
        if (s.host && !s.local && y >= Layout::people_y + Layout::person_height && y < Layout::people_y + people(s) * Layout::person_height) {
            const int person = (y - Layout::people_y) / Layout::person_height;
            if (person == s.kick_asked && x >= Layout::people_x + Layout::bonus_column)
                return {.kick = x < Layout::people_x + Layout::ready_column ? 1 : -1};
            if (x < Layout::people_x + Layout::side_column) return {.person = person};
        }
        const int own_y = Layout::people_y + s.you * Layout::person_height;
        const int side_x = Layout::people_x + Layout::side_column;
        if (s.you >= 0 && s.you < people(s) && y >= own_y && y < own_y + Layout::person_height && x >= side_x &&
            x < side_x + Layout::side_width)
            return {.side = x < side_x + Layout::side_width / 2 ? -1 : 1};
        if (y >= Layout::bonus_y && y < Layout::bonus_y + 3 * Layout::row_height)
            return {.bonus = (y - Layout::bonus_y) / Layout::row_height};
        if (y >= Layout::ai_y && y < Layout::ai_y + Layout::row_height) {
            if (x < Layout::people_x + Layout::pager_width) return {.ai = -1};
            if (x >= Layout::people_x + Layout::people_width - Layout::pager_width) return {.ai = 1};
        }
        if (!s.local && y >= Layout::delay_y && y < Layout::delay_y + Layout::row_height) {
            if (x < Layout::people_x + Layout::pager_width) return {.delay = -1};
            if (x >= Layout::people_x + Layout::people_width - Layout::pager_width) return {.delay = 1};
        }
        return {};
    }
    if (x >= Layout::preview_x && x < Layout::preview_x + Layout::preview_width + 12) {
        if (y >= Layout::manual_y && y < Layout::manual_y + Layout::row_height) return {.manual = 1};
        if (y >= Layout::manual_bonus_y && y < Layout::manual_bonus_y + Layout::row_height) return {.manual = 2};
        if (!s.local && y >= Layout::dropped_y && y < Layout::dropped_y + Layout::row_height) return {.manual = 3};
        return {};
    }
    if (x < Layout::list_x || x >= Layout::list_x + Layout::list_width) return {};
    if (s.mods_view) {
        if (y >= Layout::pager_y && y < Layout::pager_y + Layout::row_height) {
            if (x < Layout::list_x + Layout::pager_width) return {.page = -1};
            if (x >= Layout::list_x + Layout::list_width - Layout::pager_width) return {.page = 1};
            return {};
        }
        if (y < Layout::first_row_y) return {};
        const int row = (y - Layout::first_row_y) / Layout::row_height;
        const int index = s.mods_page * (Layout::visible_rows / Layout::mod_rows) + row / Layout::mod_rows;
        if (row >= Layout::visible_rows || index >= int(s.mods.size())) return {};
        return {.mod = index};
    }
    if (y >= Layout::header_y && y < Layout::first_row_y)
        return {.sort = int(x < Layout::list_x + Layout::list_width - 60 ? MapSort::Name : MapSort::People)};
    if (y >= Layout::pager_y && y < Layout::pager_y + Layout::row_height) {
        if (x < Layout::list_x + Layout::pager_width) return {.page = -1};
        if (x >= Layout::list_x + Layout::list_width - Layout::pager_width) return {.page = 1};
        return {};
    }
    if (y < Layout::first_row_y) return {};
    const int row = (y - Layout::first_row_y) / Layout::row_height;
    const int position = s.page * Layout::visible_rows + row;
    if (row >= Layout::visible_rows || position >= int(s.maps.size())) return {};
    return {.map = map_at(s, position)};
}

namespace {
std::wstring map_problem_text(const Screen &s) {
    const Language l = s.language;
    const MapProblem &p = s.map_problem;
    switch (p.kind) {
        case MapProblem::Kind::PackMissing:
            return text(l, L"This PC does not have the map mod ", L"\u041d\u0430 \u044d\u0442\u043e\u043c \u041f\u041a \u043d\u0435\u0442 \u043c\u043e\u0434\u0430 \u043a\u0430\u0440\u0442 ") + p.what +
                   text(l, L" (the Mods of Space Rangers HD or mods)", L" (\u043f\u0430\u043f\u043a\u0430 Mods \u0438\u0433\u0440\u044b \u0438\u043b\u0438 mods)");
        case MapProblem::Kind::PackDiffers:
            return text(l, L"Your map mod ", L"\u0412\u0430\u0448 \u043c\u043e\u0434 \u043a\u0430\u0440\u0442 ") + p.what +
                   text(l, L" differs from the host's", L" \u043e\u0442\u043b\u0438\u0447\u0430\u0435\u0442\u0441\u044f \u043e\u0442 \u043c\u043e\u0434\u0430 \u0445\u043e\u0441\u0442\u0430");
        case MapProblem::Kind::FilesMissing:
            return text(l, L"The map lacks files: ", L"\u041a\u0430\u0440\u0442\u0435 \u043d\u0435 \u0445\u0432\u0430\u0442\u0430\u0435\u0442 \u0444\u0430\u0439\u043b\u043e\u0432: ") + p.what +
                   (p.count > 1 ? std::wstring(text(l, L" and more", L" \u0438 \u0434\u0440\u0443\u0433\u0438\u0445")) : std::wstring());
        default: return {};
    }
}
}  // namespace

std::wstring status(const Screen &s) {
    const Language l = s.language;
    const MapRow *map = selected_row(s);
    if (!s.host) {
        if (s.loading) return text(l, L"The host is loading the battle...", L"\u0425\u043e\u0441\u0442 \u0437\u0430\u0433\u0440\u0443\u0436\u0430\u0435\u0442 \u0431\u043e\u0439...");
        for (const ModRow &m : s.mods) {
            if (!m.on || m.problem == ModRow::Problem::None) continue;
            if (m.problem == ModRow::Problem::Missing)
                return text(l, L"This PC does not have the mod ", L"\u041d\u0430 \u044d\u0442\u043e\u043c \u041f\u041a \u043d\u0435\u0442 \u043c\u043e\u0434\u0430 ") + m.name +
                       text(l, L" (mods folder)", L" (\u043f\u0430\u043f\u043a\u0430 mods)");
            return text(l, L"Your mod ", L"\u0412\u0430\u0448 \u043c\u043e\u0434 ") + m.name +
                   text(l, L" differs from the host's", L" \u043e\u0442\u043b\u0438\u0447\u0430\u0435\u0442\u0441\u044f \u043e\u0442 \u043c\u043e\u0434\u0430 \u0445\u043e\u0441\u0442\u0430");
        }
        if ((!s.missing_map.empty() || s.map_differs) && !s.map_failed)
            return text(l, L"Receiving the map from the host: ", L"\u041f\u043e\u043b\u0443\u0447\u0430\u0435\u043c \u043a\u0430\u0440\u0442\u0443 \u043e\u0442 \u0445\u043e\u0441\u0442\u0430: ") +
                   std::to_wstring(std::max(0, s.map_progress)) + L"%";
        // The host gives a map mod's map with its files: the mod's name says what failed.
        if (s.map_problem.kind == MapProblem::Kind::PackMissing || s.map_problem.kind == MapProblem::Kind::PackDiffers)
            return map_problem_text(s);
        if (!s.missing_map.empty())
            return text(l, L"This PC does not have the map ", L"\u041d\u0430 \u044d\u0442\u043e\u043c \u041f\u041a \u043d\u0435\u0442 \u043a\u0430\u0440\u0442\u044b ") + s.missing_map;
        if (!map) return text(l, L"The host is choosing a map.", L"\u0425\u043e\u0441\u0442 \u0432\u044b\u0431\u0438\u0440\u0430\u0435\u0442 \u043a\u0430\u0440\u0442\u0443.");
        if (s.map_differs)
            return text(l, L"Your file of this map differs from the host's", L"\u0412\u0430\u0448 \u0444\u0430\u0439\u043b \u044d\u0442\u043e\u0439 \u043a\u0430\u0440\u0442\u044b \u043e\u0442\u043b\u0438\u0447\u0430\u0435\u0442\u0441\u044f \u043e\u0442 \u0444\u0430\u0439\u043b\u0430 \u0445\u043e\u0441\u0442\u0430");
        if (!map_fine(s)) return map_problem_text(s);
        if (map->capacity >= 0 && !offered(s, s.selected))
            return text(l, L"The map does not take everyone; the host is choosing another.",
                        L"\u041a\u0430\u0440\u0442\u0430 \u043d\u0435 \u0432\u043c\u0435\u0449\u0430\u0435\u0442 \u0432\u0441\u0435\u0445, \u0445\u043e\u0441\u0442 \u0432\u044b\u0431\u0438\u0440\u0430\u0435\u0442 \u0434\u0440\u0443\u0433\u0443\u044e.");
        if (s.you > 0 && s.you < people(s) && s.people[s.you].ready)
            return text(l, L"You are ready. Waiting for the host to start.", L"\u0412\u044b \u0433\u043e\u0442\u043e\u0432\u044b. \u0416\u0434\u0451\u043c, \u043a\u043e\u0433\u0434\u0430 \u0445\u043e\u0441\u0442 \u043d\u0430\u0447\u043d\u0451\u0442.");
        if (sides_coincide(s))
            return text(l, L"Coinciding sides are drawn at the start. Press Ready when you are.",
                        L"\u0421\u043e\u0432\u043f\u0430\u0432\u0448\u0438\u0435 \u0441\u0442\u043e\u0440\u043e\u043d\u044b \u0440\u0430\u0441\u043f\u0440\u0435\u0434\u0435\u043b\u044f\u0442\u0441\u044f \u0441\u043b\u0443\u0447\u0430\u0439\u043d\u043e. \u041d\u0430\u0436\u043c\u0438\u0442\u0435 \"\u0413\u043e\u0442\u043e\u0432\".");
        return text(l, L"Press Ready when you are.", L"\u041d\u0430\u0436\u043c\u0438\u0442\u0435 \"\u0413\u043e\u0442\u043e\u0432\", \u043a\u043e\u0433\u0434\u0430 \u0431\u0443\u0434\u0435\u0442\u0435 \u0433\u043e\u0442\u043e\u0432\u044b.");
    }
    if (s.local) {
        if (!map) return text(l, L"Choose a map.", L"\u0412\u044b\u0431\u0435\u0440\u0438\u0442\u0435 \u043a\u0430\u0440\u0442\u0443.");
        if (map->capacity < 0) return text(l, L"Checking the map...", L"\u041f\u0440\u043e\u0432\u0435\u0440\u044f\u0435\u043c \u043a\u0430\u0440\u0442\u0443...");
        if (!map_fine(s)) return map_problem_text(s);
        if (!offered(s, s.selected))
            return text(l, L"The map has no side for the AI. Choose another.",
                        L"\u041d\u0430 \u044d\u0442\u043e\u0439 \u043a\u0430\u0440\u0442\u0435 \u043d\u0435\u0442 \u0441\u0442\u043e\u0440\u043e\u043d\u044b \u0434\u043b\u044f \u0418\u0418. \u0412\u044b\u0431\u0435\u0440\u0438\u0442\u0435 \u0434\u0440\u0443\u0433\u0443\u044e.");
        return text(l, L"The AI plays the other sides: start the battle.",
                    L"\u041e\u0441\u0442\u0430\u043b\u044c\u043d\u044b\u043c\u0438 \u0441\u0442\u043e\u0440\u043e\u043d\u0430\u043c\u0438 \u0438\u0433\u0440\u0430\u0435\u0442 \u0418\u0418, \u043c\u043e\u0436\u043d\u043e \u043d\u0430\u0447\u0438\u043d\u0430\u0442\u044c.");
    }
    if (map && !map_fine(s)) return map_problem_text(s);
    if (people(s) < 2) return text(l, L"Waiting for people: tell them the address below.", L"\u0416\u0434\u0451\u043c \u0438\u0433\u0440\u043e\u043a\u043e\u0432: \u0441\u043e\u043e\u0431\u0449\u0438\u0442\u0435 \u0438\u043c \u0430\u0434\u0440\u0435\u0441 \u043d\u0438\u0436\u0435.");
    if (!map) return text(l, L"Choose a map.", L"\u0412\u044b\u0431\u0435\u0440\u0438\u0442\u0435 \u043a\u0430\u0440\u0442\u0443.");
    if (map->capacity < 0) return text(l, L"Checking the map...", L"\u041f\u0440\u043e\u0432\u0435\u0440\u044f\u0435\u043c \u043a\u0430\u0440\u0442\u0443...");
    if (!map_fine(s)) return map_problem_text(s);
    if (!offered(s, s.selected))
        return text(l, L"The map takes up to ", L"\u041a\u0430\u0440\u0442\u0430 \u0432\u043c\u0435\u0449\u0430\u0435\u0442 \u0434\u043e ") + std::to_wstring(map->capacity) +
               text(l, L" people, the lobby has ", L" \u0438\u0433\u0440\u043e\u043a\u043e\u0432, \u0430 \u0432 \u043b\u043e\u0431\u0431\u0438 ") + std::to_wstring(people(s)) +
               text(l, L". Choose another.", L". \u0412\u044b\u0431\u0435\u0440\u0438\u0442\u0435 \u0434\u0440\u0443\u0433\u0443\u044e.");
    if (sides_coincide(s) && Lobby::clients_ready({{}, 0, s.people, {}}))
        return text(l, L"Everyone is ready. Coinciding sides are drawn at the start.", L"\u0412\u0441\u0435 \u0433\u043e\u0442\u043e\u0432\u044b. \u0421\u043e\u0432\u043f\u0430\u0432\u0448\u0438\u0435 \u0441\u0442\u043e\u0440\u043e\u043d\u044b \u0440\u0430\u0441\u043f\u0440\u0435\u0434\u0435\u043b\u044f\u0442\u0441\u044f \u0441\u043b\u0443\u0447\u0430\u0439\u043d\u043e \u043f\u0440\u0438 \u0441\u0442\u0430\u0440\u0442\u0435.");
    if (!Lobby::clients_ready({{}, 0, s.people, {}})) return text(l, L"Waiting for everyone to be ready.", L"\u0416\u0434\u0451\u043c, \u043a\u043e\u0433\u0434\u0430 \u0432\u0441\u0435 \u0431\u0443\u0434\u0443\u0442 \u0433\u043e\u0442\u043e\u0432\u044b.");
    return text(l, L"Everyone is ready: start the battle.", L"\u0412\u0441\u0435 \u0433\u043e\u0442\u043e\u0432\u044b, \u043c\u043e\u0436\u043d\u043e \u043d\u0430\u0447\u0438\u043d\u0430\u0442\u044c.");
}

bool status_is_error(const Screen &s) {
    if (!s.host) {
        // While the host's map (a map mod's: with its files) comes, the map's problem is not one yet.
        const bool coming = (!s.missing_map.empty() || s.map_differs) && !s.map_failed;
        return !s.loading && (!mods_fine(s) || (!coming && !map_fine(s)) || ((!s.missing_map.empty() || s.map_differs) && s.map_failed));
    }
    const MapRow *map = selected_row(s);
    return map && (!map_fine(s) || ((people(s) >= 2 || s.local) && map->capacity >= 0 && !offered(s, s.selected)));
}

std::wstring hint_template(const Screen &s) {
    const Language l = s.language;
    std::wstring result = frame(px(s, Layout::width), px(s, Layout::content_height));
    result += el(s, s.local  ? text(l, L"New battle", L"\u041d\u043e\u0432\u0430\u044f \u0431\u0438\u0442\u0432\u0430")
                    : s.host ? text(l, L"Network battle: you are the host", L"\u0421\u0435\u0442\u0435\u0432\u043e\u0439 \u0431\u043e\u0439: \u0432\u044b \u0445\u043e\u0441\u0442")
                             : text(l, L"Network battle", L"\u0421\u0435\u0442\u0435\u0432\u043e\u0439 \u0431\u043e\u0439"),
                 L"Font.2Ranger", text_color, 0, Layout::title_y, Layout::width, 1);

    if (s.mods_view) {
        // The battle mods, each a name with its switch and a line of what it does; the host's are its to switch.
        result += el(s, s.host ? text(l, L"Battle mods (click: on/off)", L"\u041c\u043e\u0434\u044b \u0431\u043e\u044f (\u0449\u0435\u043b\u0447\u043e\u043a: \u0432\u043a\u043b/\u0432\u044b\u043a\u043b)")
                                : text(l, L"Battle mods (the host chooses)", L"\u041c\u043e\u0434\u044b \u0431\u043e\u044f (\u0432\u044b\u0431\u0438\u0440\u0430\u0435\u0442 \u0445\u043e\u0441\u0442)"),
                     L"Font.2Small", legend_color, Layout::list_x + 12, Layout::header_y, Layout::list_width - 12, 0);
        constexpr int per_page = Layout::visible_rows / Layout::mod_rows;
        if (s.mods.empty())
            result += el(s, text(l, L"No mods: put them in mods beside the game", L"\u041c\u043e\u0434\u043e\u0432 \u043d\u0435\u0442: \u043f\u0430\u043f\u043a\u0430 mods \u0440\u044f\u0434\u043e\u043c \u0441 \u0438\u0433\u0440\u043e\u0439"),
                         L"Font.2Small", disabled_color, Layout::list_x, Layout::first_row_y, Layout::list_width, 0);
        for (int i = 0; i < per_page && s.mods_page * per_page + i < int(s.mods.size()); ++i) {
            const ModRow &mod = s.mods[std::size_t(s.mods_page * per_page + i)];
            const int y = Layout::first_row_y + i * Layout::mod_rows * Layout::row_height;
            const bool problem = mod.on && mod.problem != ModRow::Problem::None;
            const uint32_t color = problem ? error_color : mod.on ? selected_color : s.host ? text_color : disabled_color;
            constexpr int name_width = Layout::list_width - Layout::toggle_size - 8;
            const auto name = fit_row(s, mod.name, name_width);
            result += el(s, name.text, name.font, color, Layout::list_x, y + (name.font == std::wstring_view(L"Font.2Small") ? 2 : 0),
                         name_width, 0);
            // Its switch: the window draws the buttons (none in tests: no button).
            result += L"|_POS:" + std::to_wstring(px(s, Layout::list_x + Layout::list_width - Layout::toggle_size)) + L":" +
                      std::to_wstring(px(s, y)) + L"|_BITMAP:" +  // level with the name's letters
                      toggle_bitmap(mod.on) + L":B";
            const std::wstring about = !problem ? mod.description
                                       : mod.problem == ModRow::Problem::Missing ? std::wstring(text(l, L"not on this PC", L"\u043d\u0435\u0442 \u043d\u0430 \u044d\u0442\u043e\u043c \u041f\u041a"))
                                                                                 : std::wstring(text(l, L"differs from the host's", L"\u043e\u0442\u043b\u0438\u0447\u0430\u0435\u0442\u0441\u044f \u043e\u0442 \u0445\u043e\u0441\u0442\u0430"));
            // Two lines: a longer description wraps (mod.txt keeps it short). Less than the box: its margins, the stroke.
            const int about_width = px(s, Layout::list_width - 22) - 6;
            result += el(s, wrapped(s, about, sized_font(s, L"Font.2Mini").c_str(), about_width), L"Font.2Mini", problem ? error_color : legend_color, Layout::list_x + 22,
                         y + Layout::row_height - 2, Layout::list_width - 22, 0, (Layout::mod_rows - 1) * Layout::row_height - 1);
        }
        if (mods_page_count(s) > 1) {
            result += el(s, L"<", L"Font.2Normal", arrow_color, Layout::list_x, Layout::pager_y, Layout::pager_width, 0);
            result += el(s, std::to_wstring(s.mods_page + 1) + L"/" + std::to_wstring(mods_page_count(s)), L"Font.2Small",
                         legend_color, Layout::list_x + Layout::pager_width, Layout::pager_y,
                         Layout::list_width - 2 * Layout::pager_width, 1);
            result += el(s, L">", L"Font.2Normal", arrow_color, Layout::list_x + Layout::list_width - Layout::pager_width,
                         Layout::pager_y, Layout::pager_width, 0);
        }
    }
    else {
        // Maps: the ones that do not take everyone are greyed and cannot be chosen. The headers choose the
        // order; the chosen one is in the selection's colour.
        result += el(s, text(l, L"Map", L"\u041a\u0430\u0440\u0442\u0430"), L"Font.2Small",
                     s.sort == MapSort::Name ? selected_color : legend_color, Layout::list_x + 12, Layout::header_y,
                     Layout::list_width - 60, 0);
        result += el(s, text(l, L"People", L"\u0418\u0433\u0440\u043e\u043a\u043e\u0432"), L"Font.2Small",
                     s.sort == MapSort::People ? selected_color : legend_color,
                     Layout::list_x + Layout::list_width - 60, Layout::header_y, 60, 2);
        const int first = s.page * Layout::visible_rows;
        for (int row = 0; row < Layout::visible_rows && first + row < int(s.maps.size()); ++row) {
            const int index = map_at(s, first + row);
            const MapRow &map = s.maps[index];
            const uint32_t color = !offered(s, index) ? disabled_color : index == s.selected ? selected_color : text_color;
            const int y = Layout::first_row_y + row * Layout::row_height;
            const auto row_text = fit_row(s, (index == s.selected ? L"> " : L"  ") + map.name, Layout::list_width - Layout::capacity_width);
            result += el(s, row_text.text, row_text.font, color, Layout::list_x,
                         y + (row_text.font == std::wstring_view(L"Font.2Small") ? 2 : 0),
                         Layout::list_width - Layout::capacity_width, 0);
            const std::wstring capacity = map.capacity > 0 ? std::to_wstring(map.capacity) : map.capacity == 0 ? L"-" : L"..";
            result += el(s, capacity, L"Font.2Small", color, Layout::list_x + Layout::list_width - Layout::capacity_width, y,
                         Layout::capacity_width, 2);
        }
        if (page_count(s) > 1) {
            result += el(s, L"<", L"Font.2Normal", arrow_color, Layout::list_x, Layout::pager_y, Layout::pager_width, 0);
            result += el(s, std::to_wstring(s.page + 1) + L"/" + std::to_wstring(page_count(s)), L"Font.2Small",
                         legend_color, Layout::list_x + Layout::pager_width, Layout::pager_y,
                         Layout::list_width - 2 * Layout::pager_width, 1);
            result += el(s, L">", L"Font.2Normal", arrow_color, Layout::list_x + Layout::list_width - Layout::pager_width,
                         Layout::pager_y, Layout::pager_width, 0);
        }
    }

    // The lobby's map.
    const MapRow *map = selected_row(s);
    if (map && s.preview.width > 0) {
        result += L"|_POS:" + std::to_wstring(px(s, Layout::preview_x) + (px(s, Layout::preview_width) - s.preview.width) / 2) +
                  L":" + std::to_wstring(px(s, Layout::preview_y) + (px(s, Layout::preview_height) - s.preview.height) / 2) +
                  L"|_BITMAP:MapPreview:B";
    }
    else if (map) {
        result += el(s, text(l, L"No preview", L"\u041d\u0435\u0442 \u0438\u0437\u043e\u0431\u0440\u0430\u0436\u0435\u043d\u0438\u044f"), L"Font.2Small", legend_color, Layout::preview_x,
                     Layout::preview_y + Layout::preview_height / 2 - Layout::row_height / 2, Layout::preview_width, 1);
    }
    if (map || !s.missing_map.empty()) {
        // Two lines tall: a long name wraps instead of hiding its end.
        result += el(s, map ? map->name : s.missing_map, L"Font.2Normal", selected_color, Layout::preview_x, Layout::caption_y,
                     Layout::preview_width + 12, 0, Layout::map_info_y - Layout::caption_y - 2);
    }
    if (map) {
        const std::wstring info = map->capacity > 0 ? text(l, L"People: up to ", L"\u0418\u0433\u0440\u043e\u043a\u043e\u0432: \u0434\u043e ") + std::to_wstring(map->capacity)
                                  : map->capacity == 0 ? std::wstring(text(l, L"Cannot be read", L"\u041a\u0430\u0440\u0442\u0430 \u043d\u0435 \u0447\u0438\u0442\u0430\u0435\u0442\u0441\u044f"))
                                                       : std::wstring(text(l, L"People: ...", L"\u0418\u0433\u0440\u043e\u043a\u043e\u0432: ..."));
        result += el(s, info, L"Font.2Small", text_color, Layout::preview_x, Layout::map_info_y, Layout::preview_width + 12, 0);
    }

    // Direct robot control: the host switches it (and its bonus), the others see it.
    {
        const uint32_t color = s.host ? selected_color : text_color;
        const int width = Layout::preview_width + 12;
        const std::wstring on = text(l, L"on", L"\u0432\u043a\u043b"), off = text(l, L"off", L"\u0432\u044b\u043a\u043b");
        result += el(s, fit(s, text(l, L"Robot control: ", L"\u0412\u0445\u043e\u0434 \u0432 \u0440\u043e\u0431\u043e\u0442\u0430: ") + (s.manual_control ? on : off),
                                   L"Font.2Small", width),
                     L"Font.2Small", color, Layout::preview_x, Layout::manual_y, width, 0);
        result += el(s, fit(s, text(l, L"Robot bonus: ", L"\u0411\u043e\u043d\u0443\u0441 \u0432 \u0440\u043e\u0431\u043e\u0442\u0435: ") + (s.manual_bonus ? on : off),
                                   L"Font.2Small", width),
                     L"Font.2Small", s.manual_control ? color : disabled_color, Layout::preview_x,
                     Layout::manual_bonus_y, width, 0);
        result += el(s, text(l, L"bonus: reach x1.2,", L"\u0431\u043e\u043d\u0443\u0441: \u0434\u0430\u043b\u044c\u043d\u043e\u0441\u0442\u044c x1.2,"), L"Font.2Small", legend_color,
                     Layout::preview_x, Layout::manual_note_y, width, 0);
        result += el(s, text(l, L"speed x1.1", L"\u0441\u043a\u043e\u0440\u043e\u0441\u0442\u044c x1.1"), L"Font.2Small", legend_color, Layout::preview_x,
                     Layout::manual_note_y + Layout::row_height, width, 0);
    }

    // A dropped person's side: the AI plays it, or it gets no orders until they are back.
    if (!s.local) {
        const int width = Layout::preview_width + 12;
        result += el(s, fit(s, s.dropped_ai ? text(l, L"Dropped player: AI plays", L"\u0412\u044b\u043b\u0435\u0442\u0435\u0432\u0448\u0438\u0439: \u0438\u0433\u0440\u0430\u0435\u0442 \u0418\u0418")
                                            : text(l, L"Dropped player: no orders", L"\u0412\u044b\u043b\u0435\u0442\u0435\u0432\u0448\u0438\u0439: \u0431\u0435\u0437 \u043f\u0440\u0438\u043a\u0430\u0437\u043e\u0432"),
                            L"Font.2Small", width),
                     L"Font.2Small", s.host ? selected_color : text_color, Layout::preview_x, Layout::dropped_y, width, 0);
    }

    // People, under the columns' names.
    result += el(s, text(l, L"People", L"\u0418\u0433\u0440\u043e\u043a\u0438"), L"Font.2Normal", text_color, Layout::people_x, Layout::people_title_y,
                 Layout::side_column, 0);
    result += el(s, text(l, L"Side", L"\u0421\u0442\u043e\u0440\u043e\u043d\u0430"), L"Font.2Mini", legend_color, Layout::people_x + Layout::side_column,
                 Layout::people_title_y + 2, Layout::side_width, 1);
    result += el(s, text(l, L"Bonus", L"\u0411\u043e\u043d\u0443\u0441"), L"Font.2Mini", legend_color, Layout::people_x + Layout::bonus_column,
                 Layout::people_title_y + 2, Layout::bonus_width, 0);
    if (!s.local)
        result += el(s, text(l, L"Ping", L"\u041f\u0438\u043d\u0433"), L"Font.2Mini", legend_color, Layout::people_x + Layout::ping_column,
                     Layout::people_title_y + 2, Layout::ping_width, 0);
    // Without a network only this person's row: the AI's sides are not people.
    for (int i = 0; i < (s.local ? people(s) : Lobby::max_people); ++i) {
        const int y = Layout::people_y + i * Layout::person_height;
        if (i >= people(s)) {
            result += el(s, text(l, L"free", L"\u0441\u0432\u043e\u0431\u043e\u0434\u043d\u043e"), L"Font.2Small", disabled_color, Layout::people_x, y, 100, 0);
            continue;
        }
        const Lobby::Person &p = s.people[i];
        if (s.host && i == s.kick_asked && i > 0) {
            // "Rover  Send away?  Yes  No"
            const std::wstring who = !p.name.empty() ? p.name : text(l, L"Player ", L"\u0418\u0433\u0440\u043e\u043a ") + std::to_wstring(i + 1);
            result += el(s, fit(s, who, L"Font.2Small", Layout::side_column - 4), L"Font.2Small", text_color, Layout::people_x, y,
                         Layout::side_column - 4, 0);
            result += el(s, text(l, L"Send away?", L"\u0412\u044b\u0433\u043d\u0430\u0442\u044c?"), L"Font.2Small", error_color,
                         Layout::people_x + Layout::side_column, y, Layout::side_width, 1);
            result += el(s, text(l, L"Yes", L"\u0414\u0430"), L"Font.2Normal", error_color, Layout::people_x + Layout::bonus_column, y,
                         Layout::bonus_width, 1);
            result += el(s, text(l, L"No", L"\u041d\u0435\u0442"), L"Font.2Normal", selected_color, Layout::people_x + Layout::ready_column,
                         y, Layout::people_width - Layout::ready_column, 1);
            continue;
        }
        const std::wstring name = !p.name.empty() ? p.name
                                  : i == 0     ? std::wstring(text(l, L"Host", L"\u0425\u043e\u0441\u0442"))
                                               : text(l, L"Player ", L"\u0418\u0433\u0440\u043e\u043a ") + std::to_wstring(i + 1);
        const uint32_t color = i == s.you ? selected_color : text_color;
        result += el(s, fit(s, name, L"Font.2Small", Layout::side_column - 4), L"Font.2Small", color, Layout::people_x, y,
                     Layout::side_column - 4, 0);
        // The chosen side by its faction, in its colour; none chosen: any (the start gives a free one).
        const std::wstring side = p.side ? std::wstring(Factions::name(p.side, l == Language::Russian))
                                         : std::wstring(text(l, L"Any", L"\u041b\u044e\u0431\u0430\u044f"));
        const uint32_t side_color = p.side ? Factions::colour(p.side) : text_color;
        constexpr int arrow = Layout::side_arrow;
        if (i == s.you) {
            result += el(s, L"<", L"Font.2Normal", selected_color, Layout::people_x + Layout::side_column, y, arrow, 1);
            result += el(s, L">", L"Font.2Normal", selected_color,
                         Layout::people_x + Layout::side_column + Layout::side_width - arrow, y, arrow, 1);
        }
        result += el(s, side, L"Font.2Small", side_color, Layout::people_x + Layout::side_column + arrow, y,
                     Layout::side_width - 2 * arrow, 1);
        result += el(s, fit(s, p.bonus == BattleChoice::Bonus::None ? std::wstring(L"-") : bonus_label(l, p.bonus), L"Font.2Small",
                            Layout::bonus_width),
                     L"Font.2Small", color, Layout::people_x + Layout::bonus_column, y, Layout::bonus_width, 0);
        // The round trip to the host, as the host measures it: green while the shortest delay copes.
        if (i > 0) {
            const std::wstring ping = p.ping >= 0 ? std::to_wstring(p.ping) + text(l, L" ms", L" \u043c\u0441") : std::wstring(L"...");
            const uint32_t ping_color = p.ping < 0 ? legend_color : p.ping < 120 ? ready_color : p.ping < 220 ? slow_color : error_color;
            result += el(s, ping, L"Font.2Small", ping_color, Layout::people_x + Layout::ping_column, y, Layout::ping_width, 0);
        }
        if (s.local) continue;  // nobody to wait for
        const wchar_t *ready = i == 0     ? text(l, L"host", L"\u0445\u043e\u0441\u0442")
                               : p.ready ? text(l, L"ready", L"\u0433\u043e\u0442\u043e\u0432")
                                         : text(l, L"wait", L"\u0436\u0434\u0451\u043c");
        result += el(s, ready, L"Font.2Small", i > 0 && p.ready ? ready_color : legend_color,
                     Layout::people_x + Layout::ready_column, y, Layout::people_width - Layout::ready_column, 0);
    }

    // This person's bonus and the host's AI economy.
    result += el(s, text(l, L"Your bonus:", L"\u0412\u0430\u0448 \u0431\u043e\u043d\u0443\u0441:"), L"Font.2Small", text_color, Layout::people_x,
                 Layout::bonus_title_y, Layout::people_width, 0);
    const auto mine = s.you >= 0 && s.you < people(s) ? s.people[s.you].bonus : BattleChoice::Bonus::None;
    for (int i = 0; i < 3; ++i) {
        const auto bonus = static_cast<BattleChoice::Bonus>(i);
        const int y = Layout::bonus_y + i * Layout::row_height;
        result += el(s, (bonus == mine ? L"(*) " : L"( ) ") + bonus_label(l, bonus), L"Font.2Small", selected_color,
                     Layout::people_x, y, 160, 0);
        result += el(s, bonus_effect(l, bonus), L"Font.2Small", legend_color, Layout::people_x + 160, y,
                     Layout::people_width - 160, 0);
    }
    if (s.host) {
        result += el(s, L"<", L"Font.2Normal", selected_color, Layout::people_x, Layout::ai_y, Layout::pager_width, 0);
        result += el(s, L">", L"Font.2Normal", selected_color, Layout::people_x + Layout::people_width - Layout::pager_width,
                     Layout::ai_y, Layout::pager_width, 0);
    }
    result += el(s, ai_label(l, s.ai_resources), L"Font.2Small", text_color, Layout::people_x + Layout::pager_width,
                 Layout::ai_y, Layout::people_width - 2 * Layout::pager_width, 1);
    if (!s.local) {  // a network's: the delay, and the address others join at
        // The battle's delay between an order and its execution: the host's choice, auto by the pings.
        if (s.host) {
            result += el(s, L"<", L"Font.2Normal", selected_color, Layout::people_x, Layout::delay_y, Layout::pager_width, 0);
            result += el(s, L">", L"Font.2Normal", selected_color, Layout::people_x + Layout::people_width - Layout::pager_width,
                         Layout::delay_y, Layout::pager_width, 0);
        }
        result += el(s, delay_label(l, s.delay, auto_delay_ms(s)), L"Font.2Small", text_color, Layout::people_x + Layout::pager_width,
                     Layout::delay_y, Layout::people_width - 2 * Layout::pager_width, 1);

        result += el(s, s.host ? text(l, L"Others join at:", L"\u0410\u0434\u0440\u0435\u0441 \u0434\u043b\u044f \u043f\u043e\u0434\u043a\u043b\u044e\u0447\u0435\u043d\u0438\u044f:") : text(l, L"Host:", L"\u0425\u043e\u0441\u0442:"),
                     L"Font.2Small", text_color, Layout::people_x, Layout::address_y, Layout::people_width, 0);
        // The host's addresses, a line each ("a:p, b:p").
        std::wstring_view addresses = s.address;
        for (int line = 1; !addresses.empty() && line <= 4; ++line) {
            const auto comma = addresses.find(L", ");
            result += el(s, std::wstring(addresses.substr(0, comma)), L"Font.2Normal", selected_color, Layout::people_x,
                         Layout::address_y + line * Layout::row_height, Layout::people_width, 0);
            addresses = comma == std::wstring_view::npos ? std::wstring_view{} : addresses.substr(comma + 2);
        }
    }

    result += el(s, status(s), L"Font.2Small", status_is_error(s) ? error_color : text_color, Layout::list_x,
                 Layout::status_y, Layout::width - 2 * Layout::list_x, 0);
    const std::wstring arrows = s.mods_view ? text(l, L"Arrows: page.", L"\u0421\u0442\u0440\u0435\u043b\u043a\u0438: \u0441\u0442\u0440\u0430\u043d\u0438\u0446\u0430.")
                                            : text(l, L"Arrows: map.", L"\u0421\u0442\u0440\u0435\u043b\u043a\u0438: \u043a\u0430\u0440\u0442\u0430.");
    result += el(s, s.local  ? arrows + text(l, L" S: side. 1-3: bonus. M: mods. Enter: start. Esc: leave",
                                             L" S: \u0441\u0442\u043e\u0440\u043e\u043d\u0430. 1-3: \u0431\u043e\u043d\u0443\u0441. M: \u043c\u043e\u0434\u044b. Enter: \u043d\u0430\u0447\u0430\u0442\u044c. Esc: \u0432\u044b\u0439\u0442\u0438")
                    : s.host ? arrows + text(l, L" S: side. 1-3: bonus. Enter: start. Esc: leave. Click a name: send away",
                                             L" S: \u0441\u0442\u043e\u0440\u043e\u043d\u0430. 1-3: \u0431\u043e\u043d\u0443\u0441. Enter: \u043d\u0430\u0447\u0430\u0442\u044c. Esc: \u0432\u044b\u0439\u0442\u0438. \u041a\u043b\u0438\u043a \u043f\u043e \u043d\u0438\u043a\u0443: \u0432\u044b\u0433\u043d\u0430\u0442\u044c")
                             : text(l, L"S: side. 1-3: bonus. Enter: ready. Esc: leave. List headers: order",
                                    L"S: \u0441\u0442\u043e\u0440\u043e\u043d\u0430. 1-3: \u0431\u043e\u043d\u0443\u0441. Enter: \u0433\u043e\u0442\u043e\u0432. Esc: \u0432\u044b\u0439\u0442\u0438. \u0417\u0430\u0433\u043e\u043b\u043e\u0432\u043a\u0438 \u0441\u043f\u0438\u0441\u043a\u0430: \u0441\u043e\u0440\u0442\u0438\u0440\u043e\u0432\u043a\u0430"),
                 L"Font.2Small", legend_color, Layout::list_x, Layout::legend_y, Layout::width - 2 * Layout::list_x, 0);

    std::wstring primary;
    bool enabled = true;
    if (s.host) {
        primary = text(l, L"( Start )", L"( \u041d\u0430\u0447\u0430\u0442\u044c )");
        enabled = can_start(s);
    }
    else {
        const bool ready = s.you > 0 && s.you < people(s) && s.people[s.you].ready;
        primary = ready ? text(l, L"( Not ready )", L"( \u041d\u0435 \u0433\u043e\u0442\u043e\u0432 )") : text(l, L"( Ready )", L"( \u0413\u043e\u0442\u043e\u0432 )");
        enabled = !s.loading && s.missing_map.empty() && !s.map_differs && offered(s, s.selected) && mods_fine(s) &&
                  map_fine(s);
    }
    int on = 0;
    for (const ModRow &m : s.mods) on += m.on ? 1 : 0;
    result += el(s, s.mods_view ? std::wstring(text(l, L"( Maps )", L"( \u041a\u0430\u0440\u0442\u044b )"))
                                : text(l, L"( Mods: ", L"( \u041c\u043e\u0434\u044b: ") + std::to_wstring(on) + L" )",
                 L"Font.2Normal", mods_fine(s) ? selected_color : error_color, Layout::mods_x, Layout::buttons_y,
                 Layout::button_width, 1);
    result += el(s, primary, L"Font.2Normal", enabled ? selected_color : disabled_color, Layout::primary_x,
                 Layout::buttons_y, Layout::button_width, 1);
    result += el(s, text(l, L"( Leave )", L"( \u0412\u044b\u0439\u0442\u0438 )"), L"Font.2Normal", selected_color, Layout::leave_x,
                 Layout::buttons_y, Layout::button_width, 1);
    // The lines are centred on their place; the window makes their bitmaps (none in tests: no line). Without a
    // network nothing is under the AI's economy: no line there.
    for (std::size_t i = 0; i < separators().size(); ++i) {
        if (s.local && i == 5) continue;
        const Line &line = separators()[i];
        result += L"|_POS:" + std::to_wstring(px(s, line.x) - (line.vertical ? 1 : 0)) + L":" +
                  std::to_wstring(px(s, line.y) - (line.vertical ? 0 : 1)) + L"|_BITMAP:" + line_bitmap(int(i)) + L":B";
    }
    return result;
}

int join_hit(int x, int y) {
    if (y < JoinLayout::buttons_y || y >= JoinLayout::buttons_y + Layout::row_height) return 0;
    if (x >= JoinLayout::connect_x && x < JoinLayout::connect_x + JoinLayout::button_width) return 1;
    if (x >= JoinLayout::back_x && x < JoinLayout::back_x + JoinLayout::button_width) return -1;
    return 0;
}

std::wstring join_error_text(Language l, JoinError error) {
    switch (error) {
        case JoinError::BadAddress:
            return text(l, L"Type an address such as 192.168.1.5 or 192.168.1.5:27960",
                        L"\u0412\u0432\u0435\u0434\u0438\u0442\u0435 \u0430\u0434\u0440\u0435\u0441 \u0432\u0438\u0434\u0430 192.168.1.5 \u0438\u043b\u0438 192.168.1.5:27960");
        case JoinError::Unreachable:
            return text(l, L"Nobody answers at this address", L"\u041f\u043e \u044d\u0442\u043e\u043c\u0443 \u0430\u0434\u0440\u0435\u0441\u0443 \u043d\u0438\u043a\u0442\u043e \u043d\u0435 \u043e\u0442\u0432\u0435\u0447\u0430\u0435\u0442");
        case JoinError::Full: return text(l, L"The lobby is full", L"\u041b\u043e\u0431\u0431\u0438 \u0437\u0430\u043f\u043e\u043b\u043d\u0435\u043d\u043e");
        case JoinError::Version:
            return text(l, L"The host has another version of the game", L"\u0423 \u0445\u043e\u0441\u0442\u0430 \u0434\u0440\u0443\u0433\u0430\u044f \u0432\u0435\u0440\u0441\u0438\u044f \u0438\u0433\u0440\u044b");
        case JoinError::Build:
            return text(l, L"The host has another build of Planetary Battles Online", L"\u0423 \u0445\u043e\u0441\u0442\u0430 \u0434\u0440\u0443\u0433\u0430\u044f \u0441\u0431\u043e\u0440\u043a\u0430 Planetary Battles Online");
        case JoinError::Data:
            return text(l, L"Your Space Rangers HD files differ from the host's", L"\u0412\u0430\u0448\u0438 \u0444\u0430\u0439\u043b\u044b Space Rangers HD \u043e\u0442\u043b\u0438\u0447\u0430\u044e\u0442\u0441\u044f \u043e\u0442 \u0445\u043e\u0441\u0442\u0430");
        case JoinError::Name: return text(l, L"This name is already taken", L"\u0422\u0430\u043a\u043e\u0439 \u043d\u0438\u043a \u0443\u0436\u0435 \u0437\u0430\u043d\u044f\u0442");
        case JoinError::Started: return text(l, L"The battle has already started", L"\u0411\u043e\u0439 \u0443\u0436\u0435 \u043d\u0430\u0447\u0430\u043b\u0441\u044f");
        case JoinError::HostLeft: return text(l, L"The host closed the lobby", L"\u0425\u043e\u0441\u0442 \u0437\u0430\u043a\u0440\u044b\u043b \u043b\u043e\u0431\u0431\u0438");
        case JoinError::Lost: return text(l, L"The connection was lost", L"\u0421\u0432\u044f\u0437\u044c \u0441 \u0445\u043e\u0441\u0442\u043e\u043c \u043f\u043e\u0442\u0435\u0440\u044f\u043d\u0430");
        case JoinError::Busy:
            return text(l, L"Cannot open a lobby: is another one running on this PC?",
                        L"\u041d\u0435 \u0443\u0434\u0430\u043b\u043e\u0441\u044c \u043e\u0442\u043a\u0440\u044b\u0442\u044c \u043b\u043e\u0431\u0431\u0438: \u0432\u043e\u0437\u043c\u043e\u0436\u043d\u043e, \u043e\u043d\u043e \u0443\u0436\u0435 \u043e\u0442\u043a\u0440\u044b\u0442\u043e \u043d\u0430 \u044d\u0442\u043e\u043c \u041f\u041a");
        case JoinError::Kicked: return text(l, L"The host sent you away from the lobby", L"\u0425\u043e\u0441\u0442 \u0432\u044b\u0433\u043d\u0430\u043b \u0432\u0430\u0441 \u0438\u0437 \u043b\u043e\u0431\u0431\u0438");
        case JoinError::None: break;
    }
    return {};
}

std::wstring join_template(const JoinScreen &s) {
    const Language l = s.language;
    std::wstring result = frame(px(s.scale, JoinLayout::width), px(s.scale, JoinLayout::content_height));
    result += el(s.scale, text(l, L"Join a network battle", L"\u041f\u043e\u0434\u043a\u043b\u044e\u0447\u0438\u0442\u044c\u0441\u044f \u043a \u0431\u043e\u044e"), L"Font.2Ranger", text_color, 0,
                           Layout::title_y, JoinLayout::width, 1);
    result += el(s.scale, text(l, L"Host address (IP or IP:port):", L"\u0410\u0434\u0440\u0435\u0441 \u0445\u043e\u0441\u0442\u0430 (IP \u0438\u043b\u0438 IP:\u043f\u043e\u0440\u0442):"), L"Font.2Small",
                           text_color, 40, 70, JoinLayout::width - 80, 0);
    result += el(s.scale, s.address + (s.connecting ? L"" : L"_"), L"Font.2Normal", selected_color, 40, JoinLayout::field_y,
                           JoinLayout::width - 80, 0);
    result += el(s.scale, text(l, L"Digits, dots and a colon. Ctrl+V pastes.", L"\u0426\u0438\u0444\u0440\u044b, \u0442\u043e\u0447\u043a\u0438 \u0438 \u0434\u0432\u043e\u0435\u0442\u043e\u0447\u0438\u0435. Ctrl+V: \u0432\u0441\u0442\u0430\u0432\u0438\u0442\u044c."),
                           L"Font.2Small", legend_color, 40, 122, JoinLayout::width - 80, 0);
    if (s.connecting)
        result += el(s.scale, text(l, L"Connecting...", L"\u041f\u043e\u0434\u043a\u043b\u044e\u0447\u0430\u0435\u043c\u0441\u044f..."), L"Font.2Small", text_color, 40, JoinLayout::status_y,
                               JoinLayout::width - 80, 0);
    else if (s.error != JoinError::None)
        result += el(s.scale, join_error_text(l, s.error), L"Font.2Mini", error_color, 40, JoinLayout::status_y,
                               JoinLayout::width - 80, 0);
    result += el(s.scale, text(l, L"( Connect )", L"( \u041f\u043e\u0434\u043a\u043b\u044e\u0447\u0438\u0442\u044c\u0441\u044f )"), L"Font.2Normal", s.connecting ? disabled_color : selected_color,
                           JoinLayout::connect_x, JoinLayout::buttons_y, JoinLayout::button_width, 1);
    result += el(s.scale, text(l, L"( Back )", L"( \u041d\u0430\u0437\u0430\u0434 )"), L"Font.2Normal", selected_color, JoinLayout::back_x,
                           JoinLayout::buttons_y, JoinLayout::button_width, 1);
    return result;
}

int name_hit(const NameScreen &s, int x, int y) {
    if (!s.forced) return join_hit(x, y);
    const int save_x = (JoinLayout::width - JoinLayout::button_width) / 2;
    return y >= JoinLayout::buttons_y && y < JoinLayout::buttons_y + Layout::row_height && x >= save_x &&
                   x < save_x + JoinLayout::button_width
               ? 1
               : 0;
}

std::wstring name_template(const NameScreen &s) {
    const Language l = s.language;
    std::wstring result = frame(px(s.scale, JoinLayout::width), px(s.scale, JoinLayout::content_height));
    result += el(s.scale, text(l, L"Your name", L"\u0412\u0430\u0448 \u043d\u0438\u043a"), L"Font.2Ranger", text_color, 0, Layout::title_y,
                           JoinLayout::width, 1);
    result += el(s.scale, s.forced ? text(l, L"Choose the name other players will see:", L"\u0412\u044b\u0431\u0435\u0440\u0438\u0442\u0435 \u043d\u0438\u043a, \u043a\u043e\u0442\u043e\u0440\u044b\u0439 \u0443\u0432\u0438\u0434\u044f\u0442 \u0434\u0440\u0443\u0433\u0438\u0435 \u0438\u0433\u0440\u043e\u043a\u0438:")
                                    : text(l, L"The name other players see:", L"\u041d\u0438\u043a, \u043a\u043e\u0442\u043e\u0440\u044b\u0439 \u0432\u0438\u0434\u044f\u0442 \u0434\u0440\u0443\u0433\u0438\u0435 \u0438\u0433\u0440\u043e\u043a\u0438:"),
                           L"Font.2Small", text_color, 40, 70, JoinLayout::width - 80, 0);
    result += el(s.scale, s.name + L"_", L"Font.2Normal", selected_color, 40, JoinLayout::field_y, JoinLayout::width - 80, 0);
    result += el(s.scale, text(l, L"2 to 16 letters, digits, _ and -", L"\u041e\u0442 2 \u0434\u043e 16 \u0437\u043d\u0430\u043a\u043e\u0432: \u0431\u0443\u043a\u0432\u044b, \u0446\u0438\u0444\u0440\u044b, _ \u0438 -"),
                           L"Font.2Small", legend_color, 40, 122, JoinLayout::width - 80, 0);
    const bool valid = PlayerName::valid(s.name);
    result += el(s.scale, text(l, L"( Save )", L"( \u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c )"), L"Font.2Normal", valid ? selected_color : disabled_color,
                           s.forced ? (JoinLayout::width - JoinLayout::button_width) / 2 : JoinLayout::connect_x,
                           JoinLayout::buttons_y, JoinLayout::button_width, 1);
    if (!s.forced)
        result += el(s.scale, text(l, L"( Cancel )", L"( \u041e\u0442\u043c\u0435\u043d\u0430 )"), L"Font.2Normal", selected_color,
                               JoinLayout::back_x, JoinLayout::buttons_y, JoinLayout::button_width, 1);
    return result;
}

std::optional<Endpoint> parse_endpoint(const std::wstring &text) {
    Endpoint e;
    std::wstring_view address = text;
    if (const auto colon = address.find(L':'); colon != std::wstring_view::npos) {
        if (!parse_number(address.substr(colon + 1), 65535, e.port) || e.port == 0) return std::nullopt;
        address = address.substr(0, colon);
    }
    int parts = 0;
    while (true) {
        const auto dot = address.find(L'.');
        int value = 0;
        const auto part = address.substr(0, dot);
        if (part.size() > 3 || !parse_number(part, 255, value)) return std::nullopt;
        ++parts;
        if (dot == std::wstring_view::npos) break;
        address = address.substr(dot + 1);
    }
    if (parts != 4) return std::nullopt;
    for (wchar_t ch : text.substr(0, text.find(L':'))) e.address += char(ch);
    return e;
}

bool address_char(wchar_t ch) { return (ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L':'; }

}  // namespace LobbyModel
