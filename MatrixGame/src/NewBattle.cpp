// MatrixGame - licensed under GPLv2 or any later version.
#include "NewBattle.hpp"
#include "NetBattle.hpp"
#include "SettingsModel.hpp"
#include "MatrixMap.hpp"
#include "Commands.hpp"
#include "WindowDisplay.hpp"
#include "StringConstants.hpp"
#include "Interface/MatrixHint.hpp"
#include "utils.hpp"

#include <stupid_logger.hpp>

namespace NewBattle {
namespace {
// The battle menu itself is open (not a confirmation over it).
bool menu_open() {
    return available() && g_MatrixMap->m_DialogModeName && wcscmp(g_MatrixMap->m_DialogModeName, TEMPLATE_DIALOG_MENU) == 0 &&
           g_MatrixMap->m_DialogModeHints.size() == 1;
}
}  // namespace

bool available() { return Display::enabled() && g_MatrixMap && !Commands::deferred(); }

void customize_menu(std::wstring &menu_template) {
    if (!Display::enabled()) return;
    const bool russian = SettingsModel::detect_language(g_MatrixData->BlockGet(PAR_REPLACE)->ParGetNE(L"setup")) ==
                         SettingsModel::Language::Russian;
    utils::replace(menu_template, L"[help]", russian ? L"\u041d\u043e\u0432\u0430\u044f \u0431\u0438\u0442\u0432\u0430 (N)" : L"New battle (N)");
}

bool leave() {
    if (!available()) return false;
    lgr.notice("New battle: leaving the battle for the lobby");
    NetBattle::after_local_battle(true);
    g_MatrixMap->LeaveDialogMode();
    SETFLAG(g_Flags, GFLAG_EXITLOOP);
    return true;
}

bool menu_keyboard(uint8_t vk, bool down) {
    if (!menu_open()) return false;
    return down && vk == 'N' && leave();
}

bool menu_mouse_down(int x, int y) {
    if (!menu_open()) return false;
    const auto *hint = g_MatrixMap->m_DialogModeHints[0];
    x -= hint->m_PosX;
    y -= hint->m_PosY;
    return x >= 0 && x < 200 && y >= 245 && y < 275 && leave();
}

}  // namespace NewBattle
