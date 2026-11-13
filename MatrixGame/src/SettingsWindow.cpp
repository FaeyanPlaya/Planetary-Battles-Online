// MatrixGame - licensed under GPLv2 or any later version.
#include "SettingsWindow.hpp"
#include "GameLanguage.hpp"
#include "SettingsModel.hpp"
#include "StandaloneRuntime.hpp"
#include "Mods.hpp"
#include "ShaderPacks.hpp"
#include "ShaderRender.hpp"
#include "UiTheme.hpp"
#include "ModInfoWindow.hpp"
#include "GameArt.hpp"
#include "MapPacks.hpp"

#include "MatrixMap.hpp"
#include "MatrixSoundManager.hpp"
#include "Interface/CInterface.h"
#include "Interface/MatrixHint.hpp"
#include "CBitmap.hpp"
#include "WindowDisplay.hpp"
#include "input.hpp"
#include "MenuKeyLatch.hpp"
#include "KeyBindings.hpp"
#include "MatrixConfig.hpp"
#include "ModInfo.hpp"
#include "Text/Font.hpp"
#include "3g.hpp"

#include <stupid_logger.hpp>
#include <shellapi.h>

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

struct Area {
    int left, top, right, bottom;
    bool contains(int x, int y) const { return x >= left && x < right && y >= top && y < bottom; }
};
constexpr Area menu_areas[] = {{5, 100, 160, 122}, {70, 121, 122, 168}};

std::optional<SettingsModel::Window> model;
bool menu_hovered = false;  // the pointer on the entry: its icon lit (its label stays, as the other entries')
CMatrixHint *menu_icon_highlight = nullptr;
// The settings icon as the menu's other buttons look under the pointer (light disc, cyan symbol),
// made from the composed menu picture; shown over the icon while the pointer is on the entry.
constexpr const wchar_t *focused_icon_bitmap = L"MenuSetupFocused";
std::unique_ptr<CBitmap> focused_icon;
constexpr float icon_cx = 97.5f, icon_cy = 144.5f, icon_inner = 21.0f, icon_ring = 22.5f;
constexpr int icon_left = int(icon_cx - icon_ring), icon_top = int(icon_cy - icon_ring);
constexpr int icon_size = int(2 * icon_ring) + 2;
CMatrixHint *startup_hint = nullptr;                    // the window over the startup menu
std::unique_ptr<CBitmap> mod_buttons[3];                 // the mods page's switch off, on and "i"
// The shaders screens' frames (SettingsModel::frames), kept while the game runs: a few sizes and colours.
std::vector<std::pair<std::wstring, std::unique_ptr<CBitmap>>> frame_bitmaps;
bool shaders_shown = false;    // the shaders screens were shown at the last build (opened: the packs read anew)
bool folder_opened = false;    // the packs' folder was opened: read anew when the game is in front again
DWORD last_click = 0;          // a pack clicked twice: its settings
int last_click_pack = -1;

void prepare_frames() {
    for (const auto &frame : SettingsModel::frames(*model)) {
        if (std::ranges::any_of(frame_bitmaps, [&](const auto &made) { return made.first == frame.name; })) continue;
        auto bitmap = std::make_unique<CBitmap>();
        bitmap->CreateRGBA(frame.width, frame.height);
        for (int y = 0; y < frame.height; ++y)
            for (int x = 0; x < frame.width; ++x) {
                const bool edge = x == 0 || y == 0 || x == frame.width - 1 || y == frame.height - 1;
                const uint32_t color = edge ? frame.border : frame.fill;
                uint8_t *p = bitmap->Data() + y * bitmap->Pitch() + x * 4;  // B, G, R, A
                p[0] = uint8_t(color);
                p[1] = uint8_t(color >> 8);
                p[2] = uint8_t(color >> 16);
                p[3] = uint8_t(color >> 24);
            }
        CMatrixHint::SetRuntimeBitmap(frame.name, bitmap.get());
        frame_bitmaps.emplace_back(frame.name, std::move(bitmap));
    }
}

void release_frames() {
    for (const auto &made : frame_bitmaps) CMatrixHint::SetRuntimeBitmap(made.first, nullptr);
    frame_bitmaps.clear();
}

// The packs read anew: as the shaders screens open, and when the game is in front after their folder was opened.
void refresh_packs() {
    ShaderPacks::rescan();
    model->shader_menu().set_packs(ShaderPacks::installed());
}

bool game_in_front() { return GetForegroundWindow() == g_Wnd; }

void prepare_buttons() {
    if (model) {
        if (model->shaders_page() && !shaders_shown) refresh_packs();
        shaders_shown = model->shaders_page();
        model->set_shaders_problem(ShaderRender::problem());
        prepare_frames();
    }
    if (mod_buttons[0]) return;
    const GameArt::Button which[] = {GameArt::Button::SwitchOff, GameArt::Button::SwitchOn, GameArt::Button::Info};
    for (int i = 0; i < 3; ++i) mod_buttons[i] = GameArt::bitmap(GameArt::picture(which[i], SettingsModel::Layout::button_size));
    CMatrixHint::SetRuntimeBitmap(SettingsModel::switch_bitmap(false), mod_buttons[0].get());
    CMatrixHint::SetRuntimeBitmap(SettingsModel::switch_bitmap(true), mod_buttons[1].get());
    CMatrixHint::SetRuntimeBitmap(SettingsModel::info_bitmap(), mod_buttons[2].get());
}

// A mod's window or its question open over the settings: the key is its (true).
bool info_key(uint8_t vk) {
    if (!ModInfoWindow::open()) return false;
    ModInfoWindow::key(vk);
    return true;
}

// A mod switched on that needs others: the question, then (yes) the window made anew with them on.
void ask(void (*rebuilt)()) {
    if (!model->asking() || ModInfoWindow::open()) return;
    ModInfoWindow::ask(model->question(), model->language(), 1.0, [rebuilt](bool yes) {
        if (!model) return;
        model->answer(yes);
        if (yes) rebuilt();
    });
}
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
    prepare_buttons();
    UiTheme::choose(model->values().theme);  // the theme as chosen in the window, seen at once
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
    else {  // the shaders screens: their own buttons
        g_IFaceList->HideHintButton(HINT_OK);
        g_IFaceList->HideHintButton(HINT_CANCEL);
    }
    ModInfoWindow::raise();
    ask(rebuild);
}

SettingsModel::Hit hit(int x, int y) {
    const CMatrixHint *hint = g_MatrixMap->m_DialogModeHints[0];
    return model->hit(x - hint->m_PosX, y - hint->m_PosY);
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

// The personal mods of the settings' own rows that this PC lacks: not in the mods folder (read as the game started)
// or unable to load; NewSky also without PBNewSky in the game (looked for each time the window opens).
std::vector<std::string> unavailable() {
    std::vector<std::string> result;
    for (const char *id : {"farview", "interface", "firstperson", "newsky"}) {
        const Mods::Mod *mod = Mods::find(Mods::installed(), id);
        if (!mod || mod->problem != Mods::Problem::None) result.push_back(id);
    }
    if (MapPacks::new_sky_folder().empty() && std::ranges::find(result, "newsky") == result.end()) result.push_back("newsky");
    return result;
}

// The person's keys as the game has them now: in a battle the config's, before one their file over the game's.
KeyBindings::Keys current_keys() {
    auto keys = KeyBindings::load(KeyBindings::file, KeyBindings::base());
    if (g_MatrixMap) std::copy(g_Config.m_KeyActions, g_Config.m_KeyActions + KA_LAST, keys.begin());
    return keys;
}

// Saves the keys page's edits (when there are) and gives them to the game; false with the error logged.
bool save_keys(const SettingsModel::Window &window) {
    if (!window.keys_changed()) return true;
    std::string error;
    if (!KeyBindings::save(KeyBindings::file, window.keys(), KeyBindings::base(), error)) {
        lgr.error("Cannot save CFG/keys.txt: {}")(error);
        return false;
    }
    std::copy(window.keys().begin(), window.keys().end(), g_Config.m_KeyActions);
    KeyBindings::set_mod_keys(window.keys().mods);
    lgr.notice("Keys saved to CFG/keys.txt");
    return true;
}

// Saves the mods' settings (the mods page's rows of the mods folder's mods) when changed; false with the error logged.
bool save_mod_settings(const SettingsModel::Window &window) {
    if (!window.mod_settings_changed()) return true;
    std::string error;
    if (!Mods::save_settings(Mods::settings_file, window.mod_settings(), error)) {
        lgr.error("Cannot save CFG/mods.txt: {}")(error);
        return false;
    }
    Mods::set_setting_values(window.mod_settings());
    lgr.notice("Mods' settings saved to CFG/mods.txt");
    return true;
}

// What the shaders screens applied (their Done, Apply, a pack's settings opened), saved and drawn from the battle's
// next frame when it is not what the game has.
void save_shaders() {
    const auto &applied = model->shaders();
    if (applied == ShaderPacks::choice()) return;
    std::string error;
    if (ShaderPacks::save_choice(ShaderPacks::choice_file, applied, error)) lgr.notice("Shaders saved to CFG/shaders.txt");
    else {
        lgr.error("Cannot save CFG/shaders.txt: {}")(error);
        model->set_error(true);
    }
    ShaderPacks::set_choice(applied);
}

void open_folder() {
    std::error_code error;
    const auto path = std::filesystem::absolute(ShaderPacks::folder, error);
    std::filesystem::create_directories(path, error);
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    folder_opened = true;
}

// After a shaders screen's key or click: saved what it applied; Done and Cancel of the list back to the main page.
void shaders_after(ShaderMenu::After after) {
    save_shaders();
    if (after == ShaderMenu::After::MainPage) model->close_shaders();
}

void rebuild();
void startup_rebuild();

// Reset (D): asked first; yes sets the pack's settings as its author did (applied by Apply or Done).
void ask_reset() {
    if (!model->shader_menu().enabled(ShaderMenu::Button::Reset) || ModInfoWindow::open()) return;
    const bool russian = model->language() == SettingsModel::Language::Russian;
    ModInfo::Question question;
    question.mod = russian ? L"Сбросить настройки?" : L"Reset the settings?";
    question.subtitle = model->shader_menu().pack_name(model->shader_menu().chosen());
    question.lines = {russian ? L"Все настройки набора станут такими, как задал автор."
                              : L"All the pack's settings become as its author set them.",
                      russian ? L"Применятся кнопкой «Применить» или «Готово»." : L"Apply or Done applies them."};
    void (*rebuilt)() = startup_active() ? startup_rebuild : rebuild;
    ModInfoWindow::ask(question, model->language(), 1.0, [rebuilt](bool yes) {
        if (!model) return;
        if (yes) model->shader_menu().reset();
        rebuilt();
    });
}

// The shaders screens' keys (true: drawn anew).
bool shaders_key(uint8_t vk) {
    auto &menu = model->shader_menu();
    if (folder_opened && game_in_front()) {
        folder_opened = false;
        refresh_packs();
    }
    auto after = ShaderMenu::After::Stay;
    switch (vk) {
        case VK_UP: menu.move(-1); break;
        case VK_DOWN: menu.move(1); break;
        case VK_PRIOR:
        case VK_NEXT:
            for (int i = 0; i < menu.shown() - 1; ++i) menu.move(vk == VK_PRIOR ? -1 : 1);
            break;
        case VK_LEFT:
        case VK_RIGHT:
        case VK_SPACE:
            if (menu.settings()) {
                if (!menu.step(menu.row_focus(), vk == VK_LEFT ? -1 : 1) && !(vk != VK_LEFT && menu.open_row(menu.row_focus())))
                    return false;
            }
            else if (menu.focus() < 0) menu.toggle();
            else if (vk == VK_RIGHT) after = menu.press(ShaderMenu::Button::PackSettings);
            else return false;
            break;
        case VK_RETURN: after = menu.enter(); break;
        case VK_ESCAPE: after = menu.escape(); break;
        default: {
            const auto button = ShaderMenu::Menu::button_of_key(vk);  // F, S, L, A as the buttons say
            if (!menu.enabled(button)) return false;
            if (button == ShaderMenu::Button::Folder) open_folder();
            else if (button == ShaderMenu::Button::Reset) ask_reset();
            else after = menu.press(button);
        }
    }
    shaders_after(after);
    return true;
}

// A click on the shaders screens (window coordinates; true: taken).
bool shaders_click(int x, int y) {
    auto &menu = model->shader_menu();
    const auto target = menu.hit(x, y);
    auto after = ShaderMenu::After::Stay;
    switch (target.kind) {
        case ShaderMenu::Hit::Kind::None: return false;
        case ShaderMenu::Hit::Kind::On: menu.toggle(); break;
        case ShaderMenu::Hit::Kind::Pack: {
            const DWORD now = GetTickCount();
            const bool twice = target.index == last_click_pack && now - last_click <= GetDoubleClickTime();
            menu.choose(target.index);
            last_click = now;
            last_click_pack = twice ? -1 : target.index;
            if (twice) after = menu.press(ShaderMenu::Button::PackSettings);
            break;
        }
        case ShaderMenu::Hit::Kind::Row:
            menu.focus_row(target.index);
            if (!menu.open_row(target.index)) menu.step(target.index, target.direction);
            break;
        case ShaderMenu::Hit::Kind::Info: {
            ModInfo::Info info;
            info.name = menu.label(target.index);
            info.description = menu.comment(target.index);
            ModInfoWindow::show(info, model->language(), 1.0);
            return true;
        }
        case ShaderMenu::Hit::Kind::Button:
            if (target.button == ShaderMenu::Button::Folder) open_folder();
            else if (target.button == ShaderMenu::Button::Reset) ask_reset();
            else after = menu.press(target.button);
            break;
    }
    shaders_after(after);
    const auto now = menu.hit(x, y);  // another screen: the button under the pointer now
    menu.set_hover(now.kind == ShaderMenu::Hit::Kind::Button ? now.button : ShaderMenu::Button::None);
    return true;
}

// The pointer over the shaders screens: a button lit, a settings row focused (the list's packs only by a click);
// true: drawn anew.
bool shaders_move(int x, int y) {
    auto &menu = model->shader_menu();
    bool update = false;
    if (folder_opened && game_in_front()) {
        folder_opened = false;
        refresh_packs();
        update = true;
    }
    const auto target = menu.hit(x, y);
    update |= menu.set_hover(target.kind == ShaderMenu::Hit::Kind::Button ? target.button : ShaderMenu::Button::None);
    if (target.kind == ShaderMenu::Hit::Kind::Row || target.kind == ShaderMenu::Hit::Kind::Info)
        update |= menu.focus_row(target.index);
    update |= ShaderRender::problem() != model->shaders_problem();  // the drawing built what was applied
    return update;
}

// The keys page's keys (true: consumed). Waiting for a key, any key is the action's (Esc cancels).
bool keys_page_key(uint8_t vk) {
    if (model->capturing()) {
        model->key_press(vk);
        return true;
    }
    switch (vk) {
        case VK_UP: model->key_move(-1); return true;
        case VK_DOWN: model->key_move(1); return true;
        case VK_PRIOR: model->key_move(-model->rows_shown()); return true;
        case VK_NEXT: model->key_move(model->rows_shown()); return true;
        case VK_RETURN: model->key_activate(); return true;
        case VK_DELETE: model->key_default(); return true;
        case VK_ESCAPE: model->close_keys(); return true;
        default: return false;
    }
}

SettingsModel::Language language() {
    return GameLanguage::current();
}

// The window's model; the installed mods first, as they give KeyBindings their keys.
void make_model(const StandaloneSettings::Values &values) {
    const auto &mods = Mods::installed();
    model.emplace(values, adapter_modes(), language(), Mods::default_personal(mods), unavailable(), current_keys(),
                  KeyBindings::base(), mods, Mods::setting_values(), ShaderPacks::installed(), ShaderPacks::choice());
    model->set_shaders_problem(ShaderRender::problem());
    model->set_text_width([](const std::wstring &text, const wchar_t *font) {
        return int(Text::GetFont(g_D3DD, font).CalcTextWidth(text));
    });
    // The shaders screens as tall as the screen allows (480 at least: none more), at most 12 rows more.
    model->set_extra_rows(std::clamp((g_ScreenY - 600) / SettingsModel::Layout::row_height, 0, 12));
    // The other pages: the main page's every row where the screen has the room.
    model->set_rows(SettingsModel::rows_for_screen(g_ScreenY));
    shaders_shown = false;
}

void release_menu_highlight() {
    menu_hovered = false;
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
    make_model(values);
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
        if (!save_keys(*model) || !save_mod_settings(*model)) {
            model->set_error(true);
            rebuild();
            return;
        }
        StandaloneSettings::apply_runtime(values);
        lgr.notice("Settings saved to CFG/standalone.txt{}")(
                StandaloneSettings::restart_pending(values) ? "; some changes apply after restart" : "");
    }
    ModInfoWindow::close();
    model.reset();
    UiTheme::choose(StandaloneSettings::current().theme);  // the saved one (cancelled: as before)
    g_MatrixMap->EnterDialogMode(TEMPLATE_DIALOG_MENU);
}

bool keyboard(uint8_t vk, bool down) {
    if (!active()) return false;
    if (!down || info_key(vk)) return true;
    if (model->keys_page()) {
        if (keys_page_key(vk)) {
            CSound::Play(S_BCLICK);
            rebuild();
        }
        return true;
    }
    if (model->shaders_page()) {
        if (shaders_key(vk)) {
            CSound::Play(S_BCLICK);
            rebuild();
        }
        return true;
    }
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
        case VK_RETURN:
            if (SettingsModel::opener(model->selected())) {  // opens its page
                update = model->change(model->selected(), 1);
                break;
            }
            close(true);
            return true;
        case VK_ESCAPE:
            if (model->mods_page()) {  // back to the main page
                model->close_mods();
                update = true;
                break;
            }
            close(false);
            return true;
        default: return true;  // The battle stays paused; other keys are ignored while the dialog is open.
    }
    if (update) {
        CSound::Play(S_BCLICK);
        rebuild();
    }
    return true;
}

void mouse_move(int x, int y) {
    if (!active() || ModInfoWindow::open()) return;
    if (model->keys_page()) {
        const CMatrixHint *hint = g_MatrixMap->m_DialogModeHints[0];
        const int row = model->key_hit(x - hint->m_PosX, y - hint->m_PosY);
        if (row >= 0 && !model->capturing() && model->key_select(row)) rebuild();
        return;
    }
    if (model->shaders_page()) {
        const CMatrixHint *hint = g_MatrixMap->m_DialogModeHints[0];
        if (shaders_move(x - hint->m_PosX, y - hint->m_PosY)) rebuild();
        return;
    }
    const auto target = hit(x, y);
    if (target.row >= 0 && model->select(target.row)) rebuild();
}

bool mouse_down(int x, int y) {
    if (!active()) return false;
    if (model->shaders_page()) {
        if (ModInfoWindow::open()) {
            ModInfoWindow::click(x, y);
            return true;
        }
        const CMatrixHint *hint = g_MatrixMap->m_DialogModeHints[0];
        if (!shaders_click(x - hint->m_PosX, y - hint->m_PosY)) return false;
        CSound::Play(S_BCLICK);
        if (active()) rebuild();
        return true;
    }
    if (model->keys_page()) {
        const CMatrixHint *hint = g_MatrixMap->m_DialogModeHints[0];
        const int row = model->key_hit(x - hint->m_PosX, y - hint->m_PosY);
        if (row < 0) return false;  // the hint buttons
        model->key_select(row);
        model->key_activate();
        CSound::Play(S_BCLICK);
        rebuild();
        return true;
    }
    const auto target = hit(x, y);
    if (ModInfoWindow::open()) {  // any click closes a mod's window; a question takes its buttons only
        ModInfoWindow::click(x, y);
        return true;
    }
    if (target.row < 0) return false;  // The hint buttons and the rest of the screen keep their handling.
    if (target.info) {
        ModInfoWindow::show(model->info(target.row), model->language(), 1.0);
        return true;
    }
    model->select(target.row);
    model->change(target.row, target.direction);
    CSound::Play(S_BCLICK);
    rebuild();
    return true;
}

bool mouse_wheel(int notches) {
    if (!active()) return false;
    if (ModInfoWindow::open()) return true;
    if (model->keys_page() && !model->capturing()) {
        model->key_scroll(-notches);  // away from the person: up
        rebuild();
    }
    else if (model->shaders_page()) {
        model->shader_menu().scroll(-notches);
        rebuild();
    }
    else {  // the main page or the mods page
        model->scroll(-notches);
        rebuild();
    }
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
            // The original's dark teal (0.21..0.42) as grey: its disc 167..205, lit from the top (own), its symbol
            // darker (the folder) and lighter (the outlines) than that.
            const float grey = std::clamp(0.72f + 2.4f * (lum - 0.30f), 0.0f, 1.0f) * 255.0f;
            const float X = x - cx, Y = y - cy;
            const float own = 200.5f - 0.06f * X - 0.76f * Y - 0.071f * (X * X + Y * Y);
            const float symbol = grey - own;
            // As the other buttons (measured on New battle's): the disc lit from the top left, darker to the bottom
            // right; the symbol dark grey with light edges. The symbol's place is off the ring (its edge stays the disc).
            const float disc = 179.0f - 0.9f * X - 1.9f * Y;
            const float inside = std::clamp((19.5f - d) / 2.0f, 0.0f, 1.0f);
            const float shown = std::clamp(disc + inside * symbol * (symbol < 0.0f ? 1.35f : 0.7f), 0.0f, 255.0f);
            const float tint[3] = {1.0f, 0.0f, -1.0f};  // B, G, R: their grey is a touch blue
            for (int c = 0; c < 3; ++c) p[c] = uint8_t(std::clamp(p[c] * (1.0f - w) + (shown + tint[c]) * w, 0.0f, 255.0f) + 0.5f);
            // Focused: as theirs, the dark of the symbol turns cyan (its own darkness kept), its edges, the disc and
            // the ring stay (B, G, R, A).
            const int fx = x - icon_left, fy = y - icon_top;
            if (fx < 0 || fy < 0 || fx >= icon_size || fy >= icon_size) continue;
            uint8_t *f = focused_icon->Data() + fy * focused_icon->Pitch() + fx * 4;
            const float lit = std::clamp(-symbol / 40.0f, 0.0f, 1.0f) * inside * w;
            const float cyan[3] = {std::min(255.0f, shown * 2.2f), std::min(255.0f, shown * 2.1f), shown * 0.3f};
            for (int c = 0; c < 3; ++c) f[c] = uint8_t(p[c] * (1.0f - lit) + cyan[c] * lit + 0.5f);
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
    if (menu_hovered) return;
    menu_hovered = true;
    const CMatrixHint *menu = g_MatrixMap->m_DialogModeHints[0];
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
    ModInfoWindow::close();
    release_menu_highlight();
    release_focused_icon();
    release_frames();
    if (startup_hint) startup_hint->Release();
    startup_hint = nullptr;
    startup_values = nullptr;
    model.reset();
}

namespace {
void startup_rebuild() {
    prepare_buttons();
    UiTheme::choose(model->values().theme);
    CMatrixHint *hint = CMatrixHint::Build(SettingsModel::startup_template(*model), static_cast<CBlockPar *>(nullptr));
    hint->Show((g_ScreenX - hint->m_Width) / 2, (g_ScreenY - hint->m_Height) / 2);
    if (startup_hint) startup_hint->Release();
    startup_hint = hint;
    ModInfoWindow::raise();
    ask(startup_rebuild);
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
    make_model(shown);
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
        if (!save_keys(*model) || !save_mod_settings(*model)) {
            model->set_error(true);
            startup_rebuild();
            return;
        }
        Display::request(values.display.mode, values.display.fullscreen);
        Display::request_size(values.display.width, values.display.height);
        *startup_values = std::move(values);
        lgr.notice("Settings saved to CFG/standalone.txt from the startup menu");
    }
    ModInfoWindow::close();
    UiTheme::choose(startup_values->theme);  // saved above, or as before
    startup_hint->Release();
    startup_hint = nullptr;
    startup_values = nullptr;
    model.reset();
}

bool startup_keyboard(uint8_t vk, bool down) {
    if (!startup_active()) return false;
    if (!startup_keys.accept(vk, down)) return true;
    if (down && info_key(vk)) return true;
    if (model->keys_page()) {
        if (down && keys_page_key(vk)) startup_rebuild();
        return true;
    }
    if (model->shaders_page()) {
        if (down && shaders_key(vk)) startup_rebuild();
        return true;
    }
    bool update = false;
    switch (vk) {
        case VK_UP: model->move(-1); update = true; break;
        case VK_DOWN: model->move(1); update = true; break;
        case VK_LEFT:
        case VK_RIGHT: update = model->change(model->selected(), vk == VK_LEFT ? -1 : 1); break;
        case VK_RETURN:
            if (SettingsModel::opener(model->selected())) {
                update = model->change(model->selected(), 1);
                break;
            }
            startup_close(true);
            return true;
        case VK_ESCAPE:
            if (model->mods_page()) {
                model->close_mods();
                update = true;
                break;
            }
            startup_close(false);
            return true;
        default: return true;
    }
    if (update) startup_rebuild();
    return true;
}

void startup_mouse_move(int x, int y) {
    if (!startup_active() || ModInfoWindow::open()) return;
    if (model->keys_page()) {
        const int row = model->key_hit(x - startup_hint->m_PosX, y - startup_hint->m_PosY);
        if (row >= 0 && !model->capturing() && model->key_select(row)) startup_rebuild();
        return;
    }
    if (model->shaders_page()) {
        if (shaders_move(x - startup_hint->m_PosX, y - startup_hint->m_PosY)) startup_rebuild();
        return;
    }
    const auto target = model->hit(x - startup_hint->m_PosX, y - startup_hint->m_PosY);
    if (target.row >= 0 && model->select(target.row)) startup_rebuild();
}

bool startup_mouse_down(int x, int y) {
    if (!startup_active()) return false;
    if (ModInfoWindow::open()) {  // any click closes a mod's window; a question takes its buttons only
        ModInfoWindow::click(x, y);
        return true;
    }
    x -= startup_hint->m_PosX;
    y -= startup_hint->m_PosY;
    if (model->shaders_page()) {
        if (shaders_click(x, y)) startup_rebuild();
        return true;
    }
    if (const int button = SettingsModel::startup_button(*model, x, y)) {
        startup_close(button > 0);
        return true;
    }
    if (model->keys_page()) {
        const int row = model->key_hit(x, y);
        if (row >= 0) {
            model->key_select(row);
            model->key_activate();
            startup_rebuild();
        }
        return true;
    }
    const auto target = model->hit(x, y);
    if (target.row < 0) return true;
    if (target.info) {
        ModInfoWindow::show(model->info(target.row), model->language(), 1.0);
        return true;
    }
    model->select(target.row);
    model->change(target.row, target.direction);
    startup_rebuild();
    return true;
}

bool startup_mouse_wheel(int notches) {
    if (!startup_active()) return false;
    if (ModInfoWindow::open()) return true;
    if (model->keys_page() && !model->capturing()) {
        model->key_scroll(-notches);
        startup_rebuild();
    }
    else if (model->shaders_page()) {
        model->shader_menu().scroll(-notches);
        startup_rebuild();
    }
    else {
        model->scroll(-notches);
        startup_rebuild();
    }
    return true;
}

void startup_reset_input() { startup_keys = {}; }

}  // namespace SettingsWindow
