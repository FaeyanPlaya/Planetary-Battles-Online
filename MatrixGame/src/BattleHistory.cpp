// MatrixGame - licensed under GPLv2 or any later version.
#include "BattleHistory.hpp"

#include "Interface/MatrixHint.hpp"
#include "MatrixMap.hpp"
#include "SettingsModel.hpp"
#include "StringConstants.hpp"
#include "CBlockPar.hpp"
#include "3g.hpp"
#include "Text/Font.hpp"

#include <windows.h>

#include <algorithm>
#include <format>

namespace BattleHistory {
namespace {

constexpr int row = 20;
constexpr int most_width = 1100;
const std::wstring font = L"Font.2Normal";
const std::wstring more = L"• • •";
constexpr std::uint32_t title_colour = 0xFFFFD27A, dim_colour = 0xFFB0B0B0;

std::vector<Entry> g_entries;
int g_now = 0;
bool g_open = false;
bool g_armed = false;  // right Ctrl down, nothing else pressed since: its release opens or closes the panel
std::size_t g_scroll = 0;
CMatrixHint *g_panel = nullptr;
bool g_dirty = true;
int g_screen_x = 0, g_screen_y = 0;

bool russian() {
    return SettingsModel::detect_language(g_MatrixData->BlockGet(PAR_REPLACE)->ParGetNE(L"setup")) ==
           SettingsModel::Language::Russian;
}

void hide() {
    if (g_panel) g_panel->Release();
    g_panel = nullptr;
}

void build() {
    hide();
    const std::wstring templ = compose(g_entries, g_scroll, russian(), [](const std::wstring &line) {
        return int(Text::GetFont(g_D3DD, font).CalcTextWidth(line));
    });
    g_panel = CMatrixHint::Build(templ, static_cast<CBlockPar *>(nullptr));
    g_panel->Show(std::max(0, (g_ScreenX - g_panel->m_Width) / 2), std::max(0, (g_ScreenY - g_panel->m_Height) / 2));
    g_dirty = false;
    g_screen_x = g_ScreenX;
    g_screen_y = g_ScreenY;
}

void scroll_by(long long lines) {
    const std::size_t now = clamp_scroll(g_entries.size(), static_cast<long long>(g_scroll) + lines);
    if (now != g_scroll) g_dirty = true;
    g_scroll = now;
}

void set_open(bool on) {
    g_open = on;
    g_scroll = 0;
    g_dirty = true;
    if (!on) hide();
}

}  // namespace

void set_time(int ms) { g_now = ms; }

void add(const std::wstring &text, std::uint32_t colour) {
    if (text.empty()) return;
    const bool full = g_entries.size() >= kept;
    if (full) g_entries.erase(g_entries.begin());
    g_entries.push_back({g_now, text, colour});
    // Scrolled up: the same lines stay in view.
    if (g_scroll > 0 && !full) g_scroll = clamp_scroll(g_entries.size(), static_cast<long long>(g_scroll) + 1);
    g_dirty = true;
}

const std::vector<Entry> &entries() { return g_entries; }

std::wstring time_text(int ms) {
    const int s = std::max(0, ms) / 1000;
    return s >= 3600 ? std::format(L"{}:{:02}:{:02}", s / 3600, s / 60 % 60, s % 60) : std::format(L"{}:{:02}", s / 60, s % 60);
}

std::size_t clamp_scroll(std::size_t entries, long long scroll) {
    const long long most = entries > shown ? static_cast<long long>(entries - shown) : 0;
    return static_cast<std::size_t>(std::clamp<long long>(scroll, 0, most));
}

std::wstring compose(const std::vector<Entry> &entries, std::size_t scroll, bool ru,
                     const std::function<int(const std::wstring &text)> &text_width) {
    const std::wstring title = ru ? L"История боя" : L"Battle history";
    scroll = clamp_scroll(entries.size(), static_cast<long long>(scroll));
    const std::size_t end = entries.size() - scroll, first = end > shown ? end - shown : 0;
    std::vector<std::pair<std::wstring, std::uint32_t>> lines;
    for (std::size_t i = first; i < end; ++i) lines.emplace_back(time_text(entries[i].ms) + L"  " + entries[i].text, entries[i].colour);
    if (entries.empty()) lines.emplace_back(ru ? L"Сообщений пока нет" : L"No messages yet", dim_colour);
    int width = std::max(text_width(title), text_width(more));
    for (const auto &[line, colour] : lines) width = std::max(width, text_width(line));
    width = std::min(width + 12, most_width);
    // Border 0: the game's tooltip frame, as the notices. The title and the dots centred, the lines from the left (a
    // wider one wraps within most_width).
    std::wstring t = std::format(L"0|_BORDER:12:8:12:10|_FONT:{}|_TEXTP:L|_ALIGN:1:1|_WIDTH:{}|_COLOR:{:08X}|_HEIGHT:{}|_TEXT:{}", font,
                                 width, title_colour, row + 4, title);
    // The dots' rows stay when there is nothing that way (empty), so the lines do not jump while scrolling.
    const bool scrollable = entries.size() > shown;
    if (scrollable) t += std::format(L"|_COLOR:{:08X}|_HEIGHT:{}|_TEXT:{}", dim_colour, row, first > 0 ? more : L" ");
    t += L"|_ALIGN:0:1";
    for (const auto &[line, colour] : lines) {
        const int rows = std::max(1, (text_width(line) + 12 + width - 1) / width);
        t += std::format(L"|_COLOR:{:08X}|_HEIGHT:{}|_TEXT:{}", colour, rows * row, line);
    }
    if (scrollable) t += std::format(L"|_ALIGN:1:1|_COLOR:{:08X}|_HEIGHT:{}|_TEXT:{}", dim_colour, row, scroll > 0 ? more : L" ");
    return t;
}

bool open() { return g_open; }
std::size_t scroll() { return g_scroll; }

bool key(std::uint8_t vk, bool down) {
    if (vk == VK_CONTROL) {
        const bool right = (GetKeyState(VK_RCONTROL) & 0x8000) != 0;
        if (down && right) g_armed = true;  // (its autorepeat too: nothing pressed between)
        if (!down && g_armed && !right) {
            g_armed = false;
            set_open(!g_open);
        }
        return false;  // Ctrl's own state goes on for the game (Ctrl+digit, Ctrl+click)
    }
    if (down) g_armed = false;  // Ctrl+key or Ctrl+click (the mouse's buttons come here too): the game's own
    if (!g_open) return false;
    const bool ours = vk == VK_UP || vk == VK_DOWN || vk == VK_PRIOR || vk == VK_NEXT || vk == VK_ESCAPE;
    if (!down || !ours) return ours && !down;
    const long long page = static_cast<long long>(shown) - 1;
    if (vk == VK_UP) scroll_by(1);
    else if (vk == VK_DOWN) scroll_by(-1);
    else if (vk == VK_PRIOR) scroll_by(page);
    else if (vk == VK_NEXT) scroll_by(-page);
    else set_open(false);  // Esc: the panel, not the menu
    return true;
}

bool wheel(int notches) {
    if (!g_open) return false;
    scroll_by(static_cast<long long>(notches) * wheel_lines);
    return true;
}

void update() {
    if (!g_open || CMatrixMap::m_DeviceLost) {
        hide();
        return;
    }
    if (!g_panel || g_dirty || g_screen_x != g_ScreenX || g_screen_y != g_ScreenY) build();
}

void test_open(int lines) {
    set_open(true);
    scroll_by(lines);
}

void clear() {
    hide();
    g_entries.clear();
    g_now = 0;
    g_open = g_armed = false;
    g_scroll = 0;
}

}  // namespace BattleHistory
