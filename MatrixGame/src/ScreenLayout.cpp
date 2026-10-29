// MatrixGame - licensed under GPLv2 or any later version.
#include "ScreenLayout.hpp"

#include "MatrixMap.hpp"
#include "SettingsWindow.hpp"
#include "Interface/CIFaceMenu.h"
#include "Interface/CInterface.h"
#include "Interface/MatrixHint.hpp"

#include <stupid_logger.hpp>

#include <vector>

namespace ScreenLayout {
namespace {
int laid_out_width = 0;
int laid_out_height = 0;

struct DialogMove {
    int left, top, right, bottom;
    int dx, dy;
};

// Centred dialogs follow the screen centre; dialogs shown at fixed positions stay where they are.
void move_dialogs(int old_width, int old_height) {
    std::vector<DialogMove> moves;
    for (CMatrixHint *hint : g_MatrixMap->m_DialogModeHints) {
        const auto position = follow_screen({hint->m_PosX, hint->m_PosY}, hint->m_Width, hint->m_Height, old_width,
                                            old_height, g_ScreenX, g_ScreenY);
        moves.push_back({hint->m_PosX, hint->m_PosY, hint->m_PosX + hint->m_Width, hint->m_PosY + hint->m_Height,
                         position.x - hint->m_PosX, position.y - hint->m_PosY});
        hint->m_PosX = position.x;
        hint->m_PosY = position.y;
    }
    // Each visible dialog button moves with the topmost dialog that contains it.
    for (CIFaceElement *button = g_IFaceList->m_Hints->m_FirstElement; button; button = button->m_NextElement) {
        if (!button->GetVisibility())
            continue;
        const int x = Float2Int(button->m_xPos), y = Float2Int(button->m_yPos);
        for (auto move = moves.rbegin(); move != moves.rend(); ++move) {
            if (x < move->left || x >= move->right || y < move->top || y >= move->bottom)
                continue;
            if (move->dx || move->dy)
                button->RecalcPos(button->m_xPos + move->dx, button->m_yPos + move->dy, false);
            break;
        }
    }
}
}  // namespace

Position centred_dialog(int screen_width, int screen_height, int width, int height) {
    return {(screen_width - width) / 2, (screen_height - height) / 2 - Float2Int(float(screen_height) * 0.09f)};
}

int anchor(int layout_position, int layout_extent, int screen_extent) {
    return layout_position == 0 ? 0 : screen_extent - (layout_extent - layout_position);
}

Position follow_screen(Position current, int width, int height, int old_screen_width, int old_screen_height,
                       int new_screen_width, int new_screen_height) {
    if (current != centred_dialog(old_screen_width, old_screen_height, width, height))
        return current;
    return centred_dialog(new_screen_width, new_screen_height, width, height);
}

void laid_out(int width, int height) {
    laid_out_width = width;
    laid_out_height = height;
}

bool update() {
    if (laid_out_width == 0 || (g_ScreenX == laid_out_width && g_ScreenY == laid_out_height))
        return false;
    const int old_width = laid_out_width, old_height = laid_out_height;
    laid_out(g_ScreenX, g_ScreenY);

    g_MatrixMap->m_Camera.ResizeScreen();
    for (CInterface *iface = g_IFaceList->m_First; iface; iface = iface->m_NextInterface)
        iface->Reanchor(g_ScreenX - old_width, g_ScreenY - old_height);
    // Popup menus and tooltips are positioned when they open; close them so they reopen in place.
    if (FLAG(g_IFaceList->m_IfListFlags, POPUP_MENU_ACTIVE) && g_PopupMenu)
        g_PopupMenu->ResetMenu(true);
    if (g_IFaceList->m_CurrentHint) {
        g_IFaceList->m_CurrentHint->Release();
        g_IFaceList->m_CurrentHint = NULL;
        g_IFaceList->m_CurrentHintControlName.clear();
    }
    move_dialogs(old_width, old_height);
    SettingsWindow::relayout();
    lgr.notice("Interface laid out for {}x{} (was {}x{})")(g_ScreenX, g_ScreenY, old_width, old_height);
    return true;
}

}  // namespace ScreenLayout
