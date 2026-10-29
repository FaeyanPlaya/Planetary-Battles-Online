// MatrixGame - licensed under GPLv2 or any later version.
#include "BattleWait.hpp"

#include "Factions.hpp"
#include "SettingsModel.hpp"
#include "Interface/MatrixHint.hpp"
#include "MatrixMap.hpp"
#include "ScreenLayout.hpp"
#include "3g.hpp"
#include "Text/Font.hpp"

#include <algorithm>
#include <format>

namespace BattleWait {
namespace {

const wchar_t *text(bool ru, const wchar_t *english, const wchar_t *russian) { return ru ? russian : english; }

constexpr std::uint32_t text_colour = 0xFF000000;
constexpr std::uint32_t button_colour = 0xFF8E0000;  // the lobby's dark red
constexpr std::uint32_t button_hover = 0xFFE00000;
const std::wstring title_font = L"Font.2Ranger";
const std::wstring line_font = L"Font.2Normal";

int seconds(int ms) { return std::max(0, (ms + 999) / 1000); }

std::wstring button_text(const Window &window, std::size_t i, bool ru) {
    if (window.host_lost) return i == 0 ? text(ru, L"Reconnect", L"Переподключиться") : text(ru, L"Play on alone", L"Играть дальше одному");
    if (window.dropped) return i == 0 ? text(ru, L"Back to the battle", L"Вернуться в бой") : text(ru, L"Exit", L"Выйти");
    const Person &p = window.people[i];
    return text(ru, L"Go on without ", L"Продолжить без игрока ") +
           (p.name.empty() ? std::wstring(Factions::name(p.side, ru)) : p.name);
}

struct Shown {
    Window window;
    bool russian = false;
    Templates templates;
    int hover = -1;
    CMatrixHint *frame = nullptr;
    std::vector<CMatrixHint *> buttons;
    std::vector<std::array<int, 4>> rects;
};
Shown g;

bool same(const Window &a, const Window &b) {
    if (a.drop_in_ms / 1000 != b.drop_in_ms / 1000 || a.can_remove != b.can_remove || a.dropped != b.dropped ||
        a.host_lost != b.host_lost || a.catching_up != b.catching_up || a.people.size() != b.people.size())
        return false;
    for (std::size_t i = 0; i < a.people.size(); ++i) {
        const Person &x = a.people[i], &y = b.people[i];
        if (x.side != y.side || x.peer != y.peer || x.name != y.name || x.host != y.host || x.loading != y.loading ||
            seconds(x.waited_ms) != seconds(y.waited_ms))
            return false;
    }
    return true;
}

void release() {
    if (g.frame) g.frame->Release();
    g.frame = nullptr;
    for (CMatrixHint *b : g.buttons) b->Release();
    g.buttons.clear();
    g.rects.clear();
}

// (While a person back in the battle replays it nothing else is drawn: the window is.)
bool ready() { return !CMatrixMap::m_DeviceLost || CMatrixMap::m_CatchingUp; }

void build() {
    release();
    if (!ready()) return;  // no drawing while the device is lost; built once it is back
    g.templates = compose(g.window, g.russian, g.hover, [](const std::wstring &line, const std::wstring &font) {
        return int(Text::GetFont(g_D3DD, font).CalcTextWidth(line));
    });
    g.frame = CMatrixHint::Build(g.templates.window, static_cast<CBlockPar *>(nullptr));
    const auto at = ScreenLayout::centred_dialog(g_ScreenX, g_ScreenY, g.frame->m_Width, g.frame->m_Height);
    g.frame->Show(at.x, at.y);
    for (std::size_t i = 0; i < g.templates.buttons.size(); ++i) {
        CMatrixHint *b = CMatrixHint::Build(g.templates.buttons[i], static_cast<CBlockPar *>(nullptr));
        const int x = at.x + padding_x, y = at.y + g.templates.buttons_y + int(i) * row_height;
        b->Show(x, y);
        g.buttons.push_back(b);
        g.rects.push_back({x, y, g.templates.width - 2 * padding_x, row_height});
    }
}

}  // namespace

std::wstring who(const Person &p, bool ru) {
    const std::wstring faction = Factions::name(p.side, ru);
    return p.name.empty() ? faction : p.name + L" (" + faction + L")";
}

std::wstring person_line(const Person &p, bool ru) {
    if (p.loading) return who(p, ru) + text(ru, L" is loading the battle", L" загружает бой");
    return (p.host ? text(ru, L"The host ", L"Хост ") : L"") + who(p, ru) +
           std::format(L"{}{} {}", text(ru, L" is not answering: ", L" не отвечает: "),
                       seconds(p.waited_ms), text(ru, L"s", L"с"));
}

Templates compose(const Window &window, bool ru, int hover,
                  const std::function<int(const std::wstring &text, const std::wstring &font)> &text_width) {
    std::wstring title;
    std::vector<std::wstring> lines;
    if (window.catching_up >= 0) {
        title = text(ru, L"Back to the battle", L"Возвращение в бой");
        lines.push_back(std::format(L"{}{}%", text(ru, L"Catching up with the battle: ", L"Догоняем бой: "), window.catching_up));
    }
    else if (window.host_lost) {
        title = text(ru, L"The connection to the host is lost", L"Связь с хостом потеряна");
        lines.push_back(text(ru, L"The host left the game or the network is down", L"Хост вышел из игры или пропала сеть"));
    }
    else if (window.dropped) {
        title = text(ru, L"You are out of the battle", L"Вы отключены от боя");
        lines.push_back(window.dropped == 2
                            ? text(ru, L"The host went on without you", L"Хост продолжил бой без вас")
                            : text(ru, L"You did not answer for too long", L"Вы слишком долго не отвечали"));
        lines.push_back(window.dropped_ai
                            ? text(ru, L"The AI plays your side until you are back", L"Пока вас нет, вашей стороной играет ИИ")
                            : text(ru, L"Your side gets no orders until you are back", L"Пока вас нет, ваша сторона стоит без приказов"));
    }
    else {
        title = text(ru, L"Waiting for players", L"Ожидание игроков");
        for (const Person &p : window.people) lines.push_back(person_line(p, ru));
        if (window.drop_in_ms >= 0)
            lines.push_back(std::format(L"{}{} {}", text(ru, L"Removed in ", L"Отключение через "),
                                        seconds(window.drop_in_ms), text(ru, L"s", L"с")));
        if (window.can_remove)
            lines.push_back(window.dropped_ai
                                ? text(ru, L"The AI plays their side until they are back", L"Пока игрок не вернётся, его сторону ведёт ИИ")
                                : text(ru, L"Their side gets no orders until they are back", L"Пока игрок не вернётся, его сторона стоит без приказов"));
    }
    std::vector<std::wstring> buttons;
    if (window.catching_up >= 0) {
    }
    else if (window.host_lost || window.dropped) {
        buttons.push_back(button_text(window, 0, ru));
        buttons.push_back(button_text(window, 1, ru));
    }
    else if (window.can_remove)
        for (std::size_t i = 0; i < window.people.size(); ++i)
            if (window.people[i].peer >= 0) buttons.push_back(button_text(window, i, ru));

    int inner = text_width(title, title_font);
    for (const std::wstring &l : lines) inner = std::max(inner, text_width(l, line_font));
    for (const std::wstring &b : buttons) inner = std::max(inner, text_width(b, line_font));
    Templates t;
    t.width = std::max(min_width, inner + 16 + 2 * padding_x);
    inner = t.width - 2 * padding_x;

    // Each text a line of its own, row_height high, centred; the buttons' rows stay empty (their own hints).
    t.window = std::format(L"{}|_BORDER:{}:{}:{}:{}|_WIDTH:{}|_HEIGHT:{}|_ALIGN:1:1|_TEXTP:L|_COLOR:{:08X}|_FONT:{}|_TEXT:{}",
                           SettingsModel::window_border, padding_x, padding_top, padding_x, padding_bottom, inner, row_height + 6, text_colour,
                           title_font, title);
    t.window += L"|_HEIGHT:" + std::to_wstring(row_height) + L"|_FONT:" + line_font;
    for (const std::wstring &l : lines) t.window += L"|_TEXT:" + l;
    t.buttons_y = padding_top + row_height + 6 + int(lines.size()) * row_height;
    if (!buttons.empty()) t.window += std::format(L"|_DOWN:{}", 6 + int(buttons.size()) * row_height);
    t.buttons_y += 6;
    for (std::size_t i = 0; i < buttons.size(); ++i)
        t.buttons.push_back(std::format(L"-1|_FONT:{}|_WIDTH:{}|_HEIGHT:{}|_ALIGN:1:1|_COLOR:{:08X}|_TEXT:{}", line_font, inner,
                                        row_height, int(i) == hover ? button_hover : button_colour, buttons[i]));
    return t;
}

void show(const Window &window, bool russian) {
    const bool built = g.frame != nullptr;
    if (built && same(window, g.window) && russian == g.russian) return;
    g.window = window;
    g.russian = russian;
    build();
}

void hide() {
    release();
    g.window = {};
    g.hover = -1;
}

bool shown() {
    return g.frame != nullptr || !g.window.people.empty() || g.window.dropped || g.window.host_lost || g.window.catching_up >= 0;
}

int button_at(int x, int y) {
    for (std::size_t i = 0; i < g.rects.size(); ++i) {
        const auto &r = g.rects[i];
        if (x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3]) {
            if (g.window.dropped || g.window.host_lost) return int(i);
            // The i-th removable person.
            int n = -1;
            for (std::size_t p = 0; p < g.window.people.size(); ++p)
                if (g.window.people[p].peer >= 0 && ++n == int(i)) return g.window.people[p].peer;
        }
    }
    return -1;
}

void hover(int x, int y) {
    int now = -1;
    for (std::size_t i = 0; i < g.rects.size(); ++i) {
        const auto &r = g.rects[i];
        if (x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3]) now = int(i);
    }
    if (now == g.hover) return;
    g.hover = now;
    if (g.frame) build();
}

std::vector<std::array<int, 4>> button_rects() { return g.rects; }

void clear() {
    release();
    g = {};
}

}  // namespace BattleWait
