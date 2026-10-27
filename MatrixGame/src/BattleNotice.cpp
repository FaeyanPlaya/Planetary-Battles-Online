// MatrixGame - licensed under GPLv2 or any later version.
#include "BattleNotice.hpp"

#include "Interface/MatrixHint.hpp"
#include "MatrixMap.hpp"
#include "3g.hpp"
#include "Text/Font.hpp"

#include <chrono>
#include <format>
#include <utility>
#include <vector>

namespace BattleNotice {
namespace {

constexpr auto lifetime = std::chrono::seconds(7);
constexpr int top = 52;  // below the resource bar
constexpr int margin = 12;
constexpr int gap = 4;
constexpr std::size_t most = 5;

struct Notice {
    CMatrixHint *hint;
    std::chrono::steady_clock::time_point until;
};
std::vector<Notice> notices;
std::vector<std::pair<std::wstring, std::uint32_t>> waiting;  // shown once the lost device is back

bool ready() { return !CMatrixMap::m_DeviceLost; }

void place() {
    int y = top;
    for (const Notice &n : notices) {
        n.hint->Show(g_ScreenX - n.hint->m_Width - margin, y);
        y += n.hint->m_Height + gap;
    }
}

}  // namespace

void show(const std::wstring &text, std::uint32_t colour) {
    if (!ready()) {
        waiting.emplace_back(text, colour);
        return;
    }
    if (notices.size() >= most) {
        notices.front().hint->Release();
        notices.erase(notices.begin());
    }
    // Border 0: the game's tooltip frame, as wide as the text, in one colour (the hint text draws
    // colour tags over the rest of the line).
    const int width = int(Text::GetFont(g_D3DD, L"Font.2Normal").CalcTextWidth(text)) + 12;
    CMatrixHint *hint = CMatrixHint::Build(std::format(L"0|_FONT:Font.2Normal|_COLOR:{:08X}|_WIDTH:{}|_TEXT:", colour, width) + text,
                                           static_cast<CBlockPar *>(nullptr));
    notices.push_back({hint, std::chrono::steady_clock::now() + lifetime});
    place();
}

void update() {
    if (!waiting.empty() && ready())
        for (const auto &[text, colour] : std::exchange(waiting, {})) show(text, colour);
    if (notices.empty()) return;
    const auto now = std::chrono::steady_clock::now();
    bool changed = false;
    for (std::size_t i = 0; i < notices.size();) {
        if (notices[i].until > now) {
            ++i;
            continue;
        }
        notices[i].hint->Release();
        notices.erase(notices.begin() + std::ptrdiff_t(i));
        changed = true;
    }
    if (changed || (!notices.empty() && notices.front().hint->m_PosX + notices.front().hint->m_Width + margin != g_ScreenX))
        place();
}

void clear() {
    for (Notice &n : notices) n.hint->Release();
    notices.clear();
    waiting.clear();
}

}  // namespace BattleNotice
