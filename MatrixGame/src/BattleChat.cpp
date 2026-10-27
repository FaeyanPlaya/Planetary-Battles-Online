// MatrixGame - licensed under GPLv2 or any later version.
#include "BattleChat.hpp"

#include "Factions.hpp"
#include "Interface/MatrixHint.hpp"
#include "MatrixMap.hpp"
#include "3g.hpp"
#include "Text/Font.hpp"

#include <chrono>
#include <cwctype>
#include <format>
#include <vector>

namespace BattleChat {
namespace {

constexpr auto lifetime = std::chrono::seconds(15);
constexpr std::size_t most = 6;
constexpr int left = 12;
constexpr int bottom = 200;  // above the minimap and the unit panel
constexpr int gap = 4;
constexpr std::uint32_t typing_colour = 0xFFFFFFFF;

struct Message {
    std::wstring text;
    std::uint32_t colour;
    std::chrono::steady_clock::time_point until;
    CMatrixHint *hint = nullptr;
};
std::vector<Message> messages;
bool g_typing = false;
bool g_russian = false;
std::wstring g_typed;
CMatrixHint *g_line = nullptr;

bool ready() { return !CMatrixMap::m_DeviceLost; }

CMatrixHint *build(const std::wstring &text, std::uint32_t colour) {
    const int width = int(Text::GetFont(g_D3DD, L"Font.2Normal").CalcTextWidth(text)) + 12;
    return CMatrixHint::Build(std::format(L"0|_FONT:Font.2Normal|_COLOR:{:08X}|_WIDTH:{}|_TEXT:", colour, width) + text,
                              static_cast<CBlockPar *>(nullptr));
}

void release(CMatrixHint *&hint) {
    if (hint) hint->Release();
    hint = nullptr;
}

int placed_for = -1;  // the screen height the hints were placed for

// The typing line at the bottom, the messages above it, the newest lowest.
void place() {
    placed_for = g_ScreenY;
    int y = g_ScreenY - bottom;
    if (g_line) {
        y -= g_line->m_Height;
        g_line->Show(left, y);
        y -= gap;
    }
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (!it->hint) continue;
        y -= it->hint->m_Height;
        it->hint->Show(left, y);
        y -= gap;
    }
}

void rebuild_line() {
    release(g_line);
    if (!g_typing || !ready()) return;
    const wchar_t *label = g_russian ? L"Чат: " : L"Chat: ";
    g_line = build(label + g_typed + L"_", typing_colour);
    place();
}

}  // namespace

std::wstring clean(std::wstring_view text) {
    std::wstring out;
    for (wchar_t ch : text) {
        if (out.size() >= max_length) break;
        if (ch == L'\t' || ch == L'\r' || ch == L'\n') ch = L' ';
        if (std::iswcntrl(ch) || ch == L'|') continue;  // '|' would end the hint's text
        if (ch == L'<') ch = L'‹';  // no colour or other tags
        if (ch == L'>') ch = L'›';
        out += ch;
    }
    const auto first = out.find_first_not_of(L' ');
    if (first == std::wstring::npos) return {};
    return out.substr(first, out.find_last_not_of(L' ') - first + 1);
}

std::wstring line(int side, const std::wstring &name, const std::wstring &text, bool russian) {
    const std::wstring faction = Factions::name(side, russian);
    return (name.empty() ? faction : name + L" (" + faction + L")") + L": " + text;
}

bool typing() { return g_typing; }

void open(bool russian) {
    g_typing = true;
    g_russian = russian;
    g_typed.clear();
    rebuild_line();
}

void close() {
    g_typing = false;
    g_typed.clear();
    release(g_line);
    place();
}

void type(wchar_t ch) {
    if (!g_typing || ch < L' ' || g_typed.size() >= max_length) return;
    g_typed += ch;
    rebuild_line();
}

void erase_last() {
    if (!g_typing || g_typed.empty()) return;
    g_typed.pop_back();
    rebuild_line();
}

void paste(std::wstring_view text) {
    if (!g_typing) return;
    for (wchar_t ch : text) {
        if (g_typed.size() >= max_length) break;
        if (ch >= L' ') g_typed += ch;
    }
    rebuild_line();
}

std::wstring finish() {
    std::wstring text = clean(g_typed);
    close();
    return text;
}

void show(int side, const std::wstring &name, const std::wstring &text, bool russian) {
    const std::wstring clean_text = clean(text);
    if (clean_text.empty()) return;
    if (messages.size() >= most) {
        release(messages.front().hint);
        messages.erase(messages.begin());
    }
    Message m{line(side, name, clean_text, russian), Factions::bright_colour(side), std::chrono::steady_clock::now() + lifetime};
    if (ready()) m.hint = build(m.text, m.colour);
    messages.push_back(std::move(m));
    place();
}

void update() {
    const auto now = std::chrono::steady_clock::now();
    bool changed = false;
    for (std::size_t i = 0; i < messages.size();) {
        if (messages[i].until > now) {
            if (!messages[i].hint && ready()) {  // came while the device was lost
                messages[i].hint = build(messages[i].text, messages[i].colour);
                changed = true;
            }
            ++i;
            continue;
        }
        release(messages[i].hint);
        messages.erase(messages.begin() + std::ptrdiff_t(i));
        changed = true;
    }
    if (g_typing && !g_line && ready()) rebuild_line();
    if (changed || placed_for != g_ScreenY) place();
}

void clear() {
    for (Message &m : messages) release(m.hint);
    messages.clear();
    g_typing = false;
    g_typed.clear();
    release(g_line);
}

}  // namespace BattleChat
