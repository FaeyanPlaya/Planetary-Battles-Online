// MatrixGame - licensed under GPLv2 or any later version.
#include "ModInfoWindow.hpp"

#include "MatrixGame.h"
#include "Interface/MatrixHint.hpp"
#include "Text/Font.hpp"
#include "3g.hpp"

#include <cmath>
#include <optional>

namespace ModInfoWindow {
namespace {
CMatrixHint *g_hint = nullptr;
// While a question is open: it, how it is drawn and what to call with its answer.
struct Asked {
    ModInfo::Question question;
    ModInfo::Language language;
    double scale;
    std::function<void(bool)> answer;
};
std::optional<Asked> g_asked;

int text_width(const std::wstring &text, const wchar_t *font) {
    return int(Text::GetFont(g_D3DD, font).CalcTextWidth(text));
}

void build(const std::wstring &markup) {
    g_hint = CMatrixHint::Build(markup, static_cast<CBlockPar *>(nullptr));
    g_hint->Show((g_ScreenX - g_hint->m_Width) / 2, (g_ScreenY - g_hint->m_Height) / 2);
}

void release() {
    if (g_hint) g_hint->Release();
    g_hint = nullptr;
}

void answer(bool yes) {
    auto asked = std::move(g_asked);
    g_asked.reset();
    release();
    if (asked && asked->answer) asked->answer(yes);
}

}  // namespace

void show(const ModInfo::Info &info, ModInfo::Language language, double scale) {
    close();
    build(ModInfo::hint_template(info, language, scale, text_width));
}

void ask(const ModInfo::Question &question, ModInfo::Language language, double scale, std::function<void(bool)> answer) {
    close();
    build(ModInfo::question_template(question, language, scale, text_width));
    g_asked = Asked{question, language, scale, std::move(answer)};
}

void close() {
    if (g_asked) answer(false);
    release();
}

bool open() { return g_hint != nullptr; }

void raise() {
    if (g_hint) g_hint->ToTop();
}

void click(int x, int y) {
    if (!g_hint) return;
    if (!g_asked) {
        close();
        return;
    }
    const double scale = g_asked->scale;
    const int hit = ModInfo::question_hit(g_asked->question, g_asked->language, scale, text_width,
                                          int(std::floor((x - g_hint->m_PosX) / scale)),
                                          int(std::floor((y - g_hint->m_PosY) / scale)));
    if (hit >= 0) answer(hit == 1);
}

void key(int vk) {
    if (!g_hint) return;
    if (!g_asked) {
        if (vk == VK_ESCAPE || vk == VK_RETURN) close();
        return;
    }
    if (vk == VK_RETURN || vk == 'Y') answer(true);
    else if (vk == VK_ESCAPE || vk == 'N') answer(false);
}

}  // namespace ModInfoWindow
