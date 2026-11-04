// MatrixGame - licensed under GPLv2 or any later version.
#include "ModInfo.hpp"
#include "UiTheme.hpp"

#include "Mods.hpp"

#include <algorithm>
#include <cmath>

namespace ModInfo {
namespace {

uint32_t text_color() { return UiTheme::pick(0xFF000000, UiTheme::Dark::text); }
uint32_t label_color() { return UiTheme::pick(0xFF384046, UiTheme::Dark::legend); }
uint32_t title_color() { return UiTheme::pick(0xFF08498A, UiTheme::Dark::selected); }
uint32_t error_color() { return UiTheme::pick(0xFF9A0E0E, UiTheme::Dark::error); }
constexpr int text_size = 13, title_size = 16, button_size = 14;

int px(double scale, int value) { return int(std::lround(value * scale)); }

std::wstring text(Language l, const wchar_t *english, const wchar_t *russian) {
    return l == Language::Russian ? russian : english;
}

std::wstring font(double scale, int size) { return L"Font.Heavy" + std::to_wstring(std::max(6, px(scale, size))); }

// A text in a font of `size`, its box as tall as the font at this scale (the settings' box is for their own rows: a
// larger font, the title's, would be cut).
std::wstring sized_text(const std::wstring &t, double scale, int size, uint32_t color, int x, int y, int width, int align) {
    auto element = SettingsModel::text_element(t, font(scale, size).c_str(), color, x, y, width, align);
    const std::wstring key = L"|_HEIGHT:";
    if (const auto at = element.find(key); at != std::wstring::npos)
        element.replace(at, element.find(L'|', at + 1) - at, key + std::to_wstring(std::max(19, px(scale, size + 8))));
    return element;
}

int measured(const std::wstring &t, double scale, int size, const TextWidth &measure) {
    const std::wstring face = font(scale, size);
    return measure ? measure(t, face.c_str()) : int(t.size()) * px(scale, size) / 2;
}

// `line` broken at its spaces into lines of `width` pixels.
std::vector<std::wstring> broken(const std::wstring &line, double scale, int width, const TextWidth &measure) {
    std::vector<std::wstring> out;
    std::wstring current;
    for (std::size_t at = 0; at <= line.size();) {
        const std::size_t end = std::min(line.find(L' ', at), line.size());
        const std::wstring word = line.substr(at, end - at);
        const std::wstring longer = current.empty() ? word : current + L" " + word;
        if (!current.empty() && measured(longer, scale, text_size, measure) > width) {
            out.push_back(current);
            current = word;
        }
        else
            current = longer;
        at = end + 1;
    }
    if (!current.empty() || out.empty()) out.push_back(current);
    return out;
}

std::wstring joined(const std::vector<std::wstring> &names) {
    std::wstring result;
    for (const auto &name : names) result += (result.empty() ? L"" : L", ") + name;
    return result;
}

// What a window says, before its lines are broken: a title, a line under it, then paragraphs (empty: a blank line).
struct Page {
    std::wstring title{}, subtitle{};
    std::vector<Line> paragraphs{};
};

Page page(const Info &info, Language l) {
    Page p{info.name};
    p.subtitle = info.kind == Info::Kind::Battle
                     ? text(l, L"Battle mod: the host chooses it, everyone needs the same",
                            L"Мод боя: выбирает хост, нужен всем одинаковый")
                 : info.kind == Info::Kind::Personal
                     ? text(l, L"Personal mod: only what you see and how you control",
                            L"Личный мод: только то, что видите и как управляете вы")
                     : std::wstring();
    if (info.description.empty())
        p.paragraphs.push_back({text(l, L"No description", L"Описания нет"), Role::Text});
    for (std::size_t at = 0; !info.description.empty() && at <= info.description.size();) {
        const std::size_t end = std::min(info.description.find(L'\n', at), info.description.size());
        p.paragraphs.push_back({info.description.substr(at, end - at), Role::Text});
        at = end + 1;
    }
    const std::wstring none = text(l, L"none", L"нет");
    if (info.kind != Info::Kind::Other) {
        p.paragraphs.push_back({L"", Role::Text});
        p.paragraphs.push_back({text(l, L"Needs: ", L"Зависимости: ") +
                                    (info.depends.empty() ? none : joined(info.depends)),
                                Role::Label});
        p.paragraphs.push_back({text(l, L"Conflicts: ", L"Конфликты: ") +
                                    (info.conflicts.empty() ? none : joined(info.conflicts)),
                                Role::Label});
    }
    if (!info.author.empty()) {
        if (info.kind == Info::Kind::Other) p.paragraphs.push_back({L"", Role::Text});
        p.paragraphs.push_back({text(l, L"Author: ", L"Автор: ") + info.author, Role::Label});
    }
    if (!info.problem.empty()) {
        p.paragraphs.push_back({L"", Role::Text});
        p.paragraphs.push_back({info.problem, Role::Problem});
    }
    return p;
}

Page page(const Question &q, Language l) {
    Page p{q.mod, text(l, L"It needs other mods", L"Ему нужны другие моды")};
    p.paragraphs.push_back({text(l, L"It works only together with: ",
                                 L"Он работает только вместе с: ") +
                                joined(q.needs),
                            Role::Text});
    p.paragraphs.push_back({L"", Role::Text});
    p.paragraphs.push_back({text(l, L"Yes: switch them on too.", L"Да: включить и их."), Role::Label});
    p.paragraphs.push_back({text(l, L"No: it stays off.", L"Нет: он останется выключенным."), Role::Label});
    return p;
}

// As wide as its widest line (a longer one is broken), never narrower than min_width.
int width_of(const Page &p, double scale, const TextWidth &measure) {
    int widest = std::max(measured(p.title, scale, title_size, measure), measured(p.subtitle, scale, text_size, measure));
    for (const Line &line : p.paragraphs) widest = std::max(widest, measured(line.text, scale, text_size, measure));
    const int grid = int(std::ceil(widest / scale)) + 2 * Layout::margin + 4;
    return std::clamp(grid, Layout::min_width, Layout::max_width);
}

std::vector<Line> lines_of(const Page &p, int width, double scale, const TextWidth &measure) {
    std::vector<Line> out;
    for (const Line &paragraph : p.paragraphs) {
        // A "- " item's further lines go under its text, not under its mark.
        const int indent = paragraph.text.starts_with(L"- ") ? Layout::item_indent : 0;
        const auto parts = broken(paragraph.text, scale, px(scale, width - 2 * Layout::margin - indent), measure);
        for (std::size_t i = 0; i < parts.size(); ++i) out.push_back({parts[i], paragraph.role, i > 0 ? indent : 0});
    }
    return out;
}

// The window: its frame, title, the line under it and its lines; the buttons are the caller's (at height - bottom).
std::wstring drawn(const Page &p, int width, const std::vector<Line> &shown, double scale) {
    const int h = height(shown);
    std::wstring result = std::to_wstring(UiTheme::border()) + L"|_BORDER:0:0:0:0|_FONT:Font.2Normal|_WIDTH:" +
                          std::to_wstring(px(scale, width)) + L"|_HEIGHT:" + std::to_wstring(px(scale, h)) +
                          L"|_TEXTP:L|_TEXT: |_TEXTP:B";
    const auto element = [&](const std::wstring &t, int size, uint32_t color, int y, int align, int indent = 0) {
        result += sized_text(t, scale, size, color, px(scale, Layout::margin + indent), px(scale, y),
                             px(scale, width - 2 * Layout::margin - indent), align);
    };
    element(p.title, title_size, title_color(), Layout::title_y, 1);
    if (!p.subtitle.empty()) element(p.subtitle, text_size, label_color(), Layout::kind_y, 1);
    for (std::size_t i = 0; i < shown.size(); ++i) {
        const uint32_t color = shown[i].role == Role::Problem ? error_color()
                               : shown[i].role == Role::Label ? label_color()
                                                              : text_color();
        element(shown[i].text, text_size, color, Layout::first_y + int(i) * Layout::line_height, 0, shown[i].indent);
    }
    return result;
}

std::wstring button(const std::wstring &t, int centre, int y, double scale) {
    return sized_text(t, scale, button_size, title_color(), px(scale, centre - Layout::button_width / 2), px(scale, y),
                      px(scale, Layout::button_width), 1);
}

// The question's buttons: their centres.
int yes_x(int width) { return width / 2 - Layout::button_width / 2 - 10; }
int no_x(int width) { return width / 2 + Layout::button_width / 2 + 10; }

}  // namespace

Info of(const Mods::Mod &mod, const std::vector<Mods::Mod> &all, Language l) {
    const bool ru = l == Language::Russian;
    Info info;
    info.name = ru ? mod.name : mod.name_en;
    info.description = ru ? mod.description : mod.description_en;
    info.author = mod.author;
    info.kind = mod.kind == Mods::Kind::Battle ? Info::Kind::Battle : Info::Kind::Personal;
    const auto names = [&](const std::vector<std::string> &ids) {
        std::vector<std::wstring> result;
        for (const auto &id : ids) {
            // (not Mods::find: the settings' tests build this without Mods.cpp)
            const auto other = std::ranges::find(all, id, &Mods::Mod::id);
            result.push_back(other != all.end() ? (ru ? other->name : other->name_en) : std::wstring(id.begin(), id.end()));
        }
        return result;
    };
    info.depends = names(mod.depends);
    info.conflicts = names(mod.conflicts);
    if (mod.problem != Mods::Problem::None)
        info.problem = text(l, L"Cannot be used: its files are broken or made for another version of the game",
                            L"Не работает: файлы испорчены или для другой версии игры");
    return info;
}

std::vector<Line> lines(const Info &info, Language l, double scale, const TextWidth &measure) {
    const Page p = page(info, l);
    return lines_of(p, width_of(p, scale, measure), scale, measure);
}

int width(const Info &info, Language l, double scale, const TextWidth &measure) {
    return width_of(page(info, l), scale, measure);
}

int height(const std::vector<Line> &lines) {
    return Layout::first_y + int(lines.size()) * Layout::line_height + Layout::button_gap + Layout::bottom;
}

std::wstring hint_template(const Info &info, Language l, double scale, const TextWidth &measure) {
    const Page p = page(info, l);
    const int w = width_of(p, scale, measure);
    const auto shown = lines_of(p, w, scale, measure);
    return drawn(p, w, shown, scale) +
           button(text(l, L"( Close )", L"( Закрыть )"), w / 2, height(shown) - Layout::bottom, scale);
}

std::wstring question_template(const Question &q, Language l, double scale, const TextWidth &measure) {
    const Page p = page(q, l);
    const int w = width_of(p, scale, measure);
    const auto shown = lines_of(p, w, scale, measure);
    const int y = height(shown) - Layout::bottom;
    return drawn(p, w, shown, scale) + button(text(l, L"( Yes )", L"( Да )"), yes_x(w), y, scale) +
           button(text(l, L"( No )", L"( Нет )"), no_x(w), y, scale);
}

int question_hit(const Question &q, Language l, double scale, const TextWidth &measure, int x, int y) {
    const Page p = page(q, l);
    const int w = width_of(p, scale, measure);
    const int row = height(lines_of(p, w, scale, measure)) - Layout::bottom;
    if (y < row - 6 || y >= row + 24) return -1;
    if (std::abs(x - yes_x(w)) < Layout::button_width / 2) return 1;
    if (std::abs(x - no_x(w)) < Layout::button_width / 2) return 0;
    return -1;
}

}  // namespace ModInfo
