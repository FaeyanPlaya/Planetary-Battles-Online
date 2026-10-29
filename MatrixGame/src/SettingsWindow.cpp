// MatrixGame - licensed under GPLv2 or any later version.
#include "SettingsWindow.hpp"
#include "SettingsModel.hpp"
#include "StandaloneRuntime.hpp"
#include "Mods.hpp"

#include "MatrixMap.hpp"
#include "MatrixSoundManager.hpp"
#include "Interface/CInterface.h"
#include "Interface/MatrixHint.hpp"
#include "CBitmap.hpp"
#include "WindowDisplay.hpp"
#include "input.hpp"
#include "MenuKeyLatch.hpp"

#include <stupid_logger.hpp>

#include <algorithm>
#include <cstring>
#include <memory>
#include <cmath>
#include <filesystem>
#include <optional>

namespace SettingsWindow {
namespace {

constexpr const wchar_t *dialog_name = L"Settings";

// The original battle menu draws its settings entry in a disabled color; the standalone game
// enables it and adds a click area over the label and the baked-in button.
constexpr const wchar_t *menu_label_disabled = L"_COLOR:FF6794A3|_POS:6:105|_TEXT:[setup]";
constexpr const wchar_t *menu_label_enabled = L"_COLOR:FFADA793|_POS:6:105|_TEXT:[setup]";
constexpr const wchar_t *menu_label_highlight = L"-1|_FONT:Font.2Ranger|_WIDTH:200|_COLOR:FF6FE4FF|_TEXT:[setup]";
constexpr int menu_label_x = 6;
constexpr int menu_label_y = 105;

struct Area {
    int left, top, right, bottom;
    bool contains(int x, int y) const { return x >= left && x < right && y >= top && y < bottom; }
};
constexpr Area menu_areas[] = {{5, 100, 160, 122}, {70, 121, 122, 168}};

std::optional<SettingsModel::Window> model;
CMatrixHint *menu_highlight = nullptr;
CMatrixHint *menu_icon_highlight = nullptr;
// The settings icon as the menu's other buttons look under the pointer (light disc, cyan symbol),
// made from the composed menu picture; shown over the icon while the pointer is on the entry.
constexpr const wchar_t *focused_icon_bitmap = L"MenuSetupFocused";
std::unique_ptr<CBitmap> focused_icon;
constexpr float icon_cx = 97.5f, icon_cy = 144.5f, icon_inner = 21.0f, icon_ring = 22.5f;
constexpr int icon_left = int(icon_cx - icon_ring), icon_top = int(icon_cy - icon_ring);
constexpr int icon_size = int(2 * icon_ring) + 2;
CMatrixHint *startup_hint = nullptr;                    // the window over the startup menu
StandaloneSettings::Values *startup_values = nullptr;   // where its Apply puts the settings
MenuKeyLatch startup_keys;

bool menu_ready() {
    return g_MatrixMap && g_MatrixMap->m_DialogModeName &&
           wcscmp(g_MatrixMap->m_DialogModeName, TEMPLATE_DIALOG_MENU) == 0 &&
           g_MatrixMap->m_DialogModeHints.size() == 1;
}

bool inside_menu_entry(int x, int y) {
    const CMatrixHint *menu = g_MatrixMap->m_DialogModeHints[0];
    for (const auto &area : menu_areas) {
        if (area.contains(x - menu->m_PosX, y - menu->m_PosY)) return true;
    }
    return false;
}

void apply_handler() { close(true); }

void cancel_handler() { close(false); }

void rebuild() {
    auto &hints = g_MatrixMap->m_DialogModeHints;
    CMatrixHint *hint = CMatrixHint::Build(SettingsModel::hint_template(*model), static_cast<CBlockPar *>(nullptr));
    const int x = (g_ScreenX - hint->m_Width) / 2;
    const int y = (g_ScreenY - hint->m_Height) / 2;
    hint->Show(x, y);
    if (hints.empty()) {
        hints.push_back(hint);
    }
    else {
        hints[0]->Release();
        hints[0] = hint;
    }
    if (hint->GetCopyPosCnt() > 1) {
        g_IFaceList->CreateHintButton(x + hint->GetCopyPos(0).x, y + hint->GetCopyPos(0).y, HINT_OK, apply_handler);
        g_IFaceList->CreateHintButton(x + hint->GetCopyPos(1).x, y + hint->GetCopyPos(1).y, HINT_CANCEL,
                                      cancel_handler);
    }
}

SettingsModel::Hit hit(int x, int y) {
    const CMatrixHint *hint = g_MatrixMap->m_DialogModeHints[0];
    return SettingsModel::hit(x - hint->m_PosX, y - hint->m_PosY);
}

std::vector<SettingsModel::Resolution> adapter_modes() {
    std::vector<SettingsModel::Resolution> modes;
    const UINT count = g_D3D->GetAdapterModeCount(D3DADAPTER_DEFAULT, D3DFMT_X8R8G8B8);
    for (UINT i = 0; i < count; ++i) {
        D3DDISPLAYMODE mode{};
        if (SUCCEEDED(g_D3D->EnumAdapterModes(D3DADAPTER_DEFAULT, D3DFMT_X8R8G8B8, i, &mode)))
            modes.push_back({static_cast<int>(mode.Width), static_cast<int>(mode.Height)});
    }
    return modes;
}

SettingsModel::Language language() {
    return SettingsModel::detect_language(g_MatrixData->BlockGet(PAR_REPLACE)->ParGetNE(L"setup"));
}

void release_menu_highlight() {
    if (menu_highlight) menu_highlight->Release();
    menu_highlight = nullptr;
    if (menu_icon_highlight) menu_icon_highlight->Release();
    menu_icon_highlight = nullptr;
}

void release_focused_icon() {
    CMatrixHint::SetRuntimeBitmap(focused_icon_bitmap, nullptr);
    focused_icon.reset();
}

}  // namespace

// Only the standalone EXE manages its own window display; a DLL host never enables it.
bool available() { return Display::enabled(); }

bool active() {
    return model && g_MatrixMap && g_MatrixMap->m_DialogModeName &&
           wcscmp(g_MatrixMap->m_DialogModeName, dialog_name) == 0 && !g_MatrixMap->m_DialogModeHints.empty();
}

void open() {
    if (!available() || !g_MatrixMap) return;
    SettingsModel::lock_battle_rules(StandaloneSettings::battle_rules_locked());
    auto values = StandaloneSettings::current();
    values.display.mode = Display::current_mode();  // Alt+Enter changes are shown and saved by Apply.
    values.display.fullscreen = Display::fullscreen_mode();
    // A rejected or unavailable size keeps the one in use, so the window shows that size.
    const auto size = Display::current_size();
    values.display.width = size.width;
    values.display.height = size.height;
    model.emplace(values, adapter_modes(), language(), Mods::default_personal(Mods::installed()));
    resume();
}

void resume() {
    if (!available() || !g_MatrixMap || !model) return;
    g_MatrixMap->LeaveDialogMode();
    g_MatrixMap->Pause(true);
    SETFLAG(g_MatrixMap->m_Flags, MMFLAG_DIALOG_MODE);
    g_MatrixMap->m_DialogModeName = dialog_name;
    CSound::Play(L"Formopen");
    rebuild();
}

void close(bool apply) {
    if (!active()) return;
    if (apply) {
        const auto values = model->values();
        std::string error;
        if (!StandaloneSettings::save(values, error)) {
            lgr.error("Cannot save CFG/standalone.txt: {}")(error);
            model->set_error(true);
            rebuild();
            return;
        }
        StandaloneSettings::apply_runtime(values);
        lgr.notice("Settings saved to CFG/standalone.txt{}")(
                StandaloneSettings::restart_pending(values) ? "; some changes apply after restart" : "");
    }
    model.reset();
    g_MatrixMap->EnterDialogMode(TEMPLATE_DIALOG_MENU);
}

bool keyboard(uint8_t vk, bool down) {
    if (!active()) return false;
    if (!down) return true;
    bool update = false;
    switch (vk) {
        case VK_UP:
            model->move(-1);
            update = true;
            break;
        case VK_DOWN:
            model->move(1);
            update = true;
            break;
        case VK_LEFT:
        case VK_RIGHT:
            update = model->change(model->selected(), vk == VK_LEFT ? -1 : 1);
            break;
        case VK_RETURN: close(true); return true;
        case VK_ESCAPE: close(false); return true;
        default: return true;  // The battle stays paused; other keys are ignored while the dialog is open.
    }
    if (update) {
        CSound::Play(S_BCLICK);
        rebuild();
    }
    return true;
}

void mouse_move(int x, int y) {
    if (!active()) return;
    const auto target = hit(x, y);
    if (target.row >= 0 && model->select(target.row)) rebuild();
}

bool mouse_down(int x, int y) {
    if (!active()) return false;
    const auto target = hit(x, y);
    if (target.row < 0) return false;  // The hint buttons and the rest of the screen keep their handling.
    model->select(target.row);
    model->change(target.row, target.direction);
    CSound::Play(S_BCLICK);
    rebuild();
    return true;
}

void customize_menu(std::wstring &menu_template) {
    if (!available()) return;
    utils::replace(menu_template, menu_label_disabled, menu_label_enabled);
}

void paint_menu_icon(CBitmap &menu) {
    if (!available() || menu.BytePP() < 3) return;
    // The icon's disc (inside its black ring) in the menu picture, lit from the top left.
    constexpr float cx = icon_cx, cy = icon_cy, inner = icon_inner, ring = icon_ring;
    release_menu_highlight();
    release_focused_icon();
    focused_icon = std::make_unique<CBitmap>();
    focused_icon->CreateRGBA(icon_size, icon_size);
    std::memset(focused_icon->Data(), 0, std::size_t(focused_icon->Pitch()) * icon_size);
    for (int y = int(cy - ring); y <= int(cy + ring); ++y)
        for (int x = int(cx - ring); x <= int(cx + ring); ++x) {
            if (x < 0 || y < 0 || x >= menu.SizeX() || y >= menu.SizeY()) continue;
            const float d = std::hypot(x - cx, y - cy);
            if (d >= ring) continue;
            const float w = d <= inner ? 1.0f : (ring - d) / (ring - inner);
            uint8_t *p = menu.Data() + y * menu.Pitch() + x * menu.BytePP();  // B, G, R[, A]
            const float lum = (0.11f * p[0] + 0.59f * p[1] + 0.3f * p[2]) / 255.0f;
            // Light grey disc and dark symbol, as on the other buttons (dark teal 0.21..0.42 in).
            const float grey = std::clamp(0.72f + 2.4f * (lum - 0.30f), 0.0f, 1.0f) * 255.0f;  // the ring stays dark
            for (int c = 0; c < 3; ++c) p[c] = uint8_t(p[c] * (1.0f - w) + grey * w + 0.5f);
            // Focused: the dark symbol turns cyan, the light disc stays (B, G, R, A).
            const int fx = x - icon_left, fy = y - icon_top;
            if (fx < 0 || fy < 0 || fx >= icon_size || fy >= icon_size) continue;
            uint8_t *f = focused_icon->Data() + fy * focused_icon->Pitch() + fx * 4;
            // The repainted disc is 0.55..0.9 bright, its symbol 0.4..0.5.
            const float symbol = std::clamp((0.56f - grey / 255.0f) / 0.12f, 0.0f, 1.0f) * w;
            const float cyan[3] = {245.0f, 214.0f, 64.0f};
            for (int c = 0; c < 3; ++c) f[c] = uint8_t(p[c] * (1.0f - symbol) + cyan[c] * symbol + 0.5f);
            f[3] = uint8_t(255.0f * std::min(1.0f, (ring - d) * 2.0f) + 0.5f);
        }
    CMatrixHint::SetRuntimeBitmap(focused_icon_bitmap, focused_icon.get());
}

bool menu_mouse_down(int x, int y) {
    if (!available() || !menu_ready() || !inside_menu_entry(x, y)) return false;
    release_menu_highlight();
    CSound::Play(S_BCLICK);
    open();
    return true;
}

void menu_mouse_move(int x, int y) {
    if (!available() || !menu_ready() || !inside_menu_entry(x, y)) {
        release_menu_highlight();
        return;
    }
    if (menu_highlight) return;
    const CMatrixHint *menu = g_MatrixMap->m_DialogModeHints[0];
    menu_highlight = CMatrixHint::Build(menu_label_highlight, g_MatrixData->BlockGet(PAR_REPLACE));
    menu_highlight->Show(menu->m_PosX + menu_label_x, menu->m_PosY + menu_label_y);
    if (focused_icon) {
        menu_icon_highlight = CMatrixHint::Build(std::wstring(L"-1|_BITMAP:") + focused_icon_bitmap + L":B",
                                                 static_cast<CBlockPar *>(nullptr));
        menu_icon_highlight->Show(menu->m_PosX + icon_left, menu->m_PosY + icon_top);
    }
    CSound::Play(S_BENTER);
}

void menu_closed() { release_menu_highlight(); }

void relayout() {
    release_menu_highlight();  // Recreated at the menu's new position on the next pointer move.
    if (active()) rebuild();
}

void shutdown() {
    release_menu_highlight();
    release_focused_icon();
    if (startup_hint) startup_hint->Release();
    startup_hint = nullptr;
    startup_values = nullptr;
    model.reset();
}

namespace {
void startup_rebuild() {
    CMatrixHint *hint = CMatrixHint::Build(SettingsModel::startup_template(*model), static_cast<CBlockPar *>(nullptr));
    hint->Show((g_ScreenX - hint->m_Width) / 2, (g_ScreenY - hint->m_Height) / 2);
    if (startup_hint) startup_hint->Release();
    startup_hint = hint;
}
}  // namespace

void open_startup(StandaloneSettings::Values &values) {
    if (!available() || g_MatrixMap) return;
    SettingsModel::lock_battle_rules(false);
    auto shown = values;
    shown.display.mode = Display::current_mode();
    shown.display.fullscreen = Display::fullscreen_mode();
    const auto size = Display::current_size();
    shown.display.width = size.width;
    shown.display.height = size.height;
    model.emplace(shown, adapter_modes(), language(), Mods::default_personal(Mods::installed()));
    startup_values = &values;
    startup_keys = {Input::isVKeyPressed(VK_RETURN) || (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0,
                    Input::isVKeyPressed(VK_ESCAPE) || (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0};
    startup_rebuild();
}

bool startup_active() { return model && startup_hint; }

void startup_close(bool apply) {
    if (!startup_active()) return;
    if (apply) {
        auto values = model->values();
        std::string error;
        if (!StandaloneSettings::save(values, error)) {
            lgr.error("Cannot save CFG/standalone.txt: {}")(error);
            model->set_error(true);
            startup_rebuild();
            return;
        }
        Display::request(values.display.mode, values.display.fullscreen);
        Display::request_size(values.display.width, values.display.height);
        *startup_values = std::move(values);
        lgr.notice("Settings saved to CFG/standalone.txt from the startup menu");
    }
    startup_hint->Release();
    startup_hint = nullptr;
    startup_values = nullptr;
    model.reset();
}

bool startup_keyboard(uint8_t vk, bool down) {
    if (!startup_active()) return false;
    if (!startup_keys.accept(vk, down)) return true;
    bool update = false;
    switch (vk) {
        case VK_UP: model->move(-1); update = true; break;
        case VK_DOWN: model->move(1); update = true; break;
        case VK_LEFT:
        case VK_RIGHT: update = model->change(model->selected(), vk == VK_LEFT ? -1 : 1); break;
        case VK_RETURN: startup_close(true); return true;
        case VK_ESCAPE: startup_close(false); return true;
        default: return true;
    }
    if (update) startup_rebuild();
    return true;
}

void startup_mouse_move(int x, int y) {
    if (!startup_active()) return;
    const auto target = SettingsModel::hit(x - startup_hint->m_PosX, y - startup_hint->m_PosY);
    if (target.row >= 0 && model->select(target.row)) startup_rebuild();
}

bool startup_mouse_down(int x, int y) {
    if (!startup_active()) return false;
    x -= startup_hint->m_PosX;
    y -= startup_hint->m_PosY;
    if (const int button = SettingsModel::startup_button(x, y)) {
        startup_close(button > 0);
        return true;
    }
    const auto target = SettingsModel::hit(x, y);
    if (target.row < 0) return true;
    model->select(target.row);
    model->change(target.row, target.direction);
    startup_rebuild();
    return true;
}

void startup_reset_input() { startup_keys = {}; }

}  // namespace SettingsWindow
