// MatrixGame - licensed under GPLv2 or any later version.
#include "MapWindow.hpp"
#include "MapCatalog.hpp"
#include "MapModel.hpp"
#include "MapSelection.hpp"
#include "MapMetadata.hpp"
#include "MapPreview.hpp"
#include "BattleLoading.hpp"
#include "MenuKeyLatch.hpp"
#include "input.hpp"
#include "Text/Font.hpp"
#include "StandaloneRuntime.hpp"

#include "MatrixMap.hpp"
#include "Commands.hpp"
#include "MatrixSoundManager.hpp"
#include "Interface/CInterface.h"
#include "Interface/MatrixHint.hpp"
#include "WindowDisplay.hpp"

#include "CException.hpp"
#include "utils.hpp"

#include <stupid_logger.hpp>

#include <memory>
#include <optional>

namespace MapWindow {
namespace {

constexpr const wchar_t *dialog_name = L"Map";

struct State {
    Origin origin;
    StandaloneSettings::Values values;
    MapModel::Window model;
    MenuKeyLatch actions{};
};

std::optional<State> state;
CMatrixHint *startup_hint = nullptr;
std::optional<StandaloneSettings::Values> startup_choice;
CMatrixHint *current_hint() {
    if (state && state->origin == Origin::Startup) return startup_hint;
    return g_MatrixMap && !g_MatrixMap->m_DialogModeHints.empty() ? g_MatrixMap->m_DialogModeHints[0] : nullptr;
}

void load_preview() {
    const auto *map = state->model.selected_map();
    MapPreview::show(map ? MapPreview::source(*map) : std::wstring{});
}


void rebuild() {
    load_preview();
    CMatrixHint *hint = CMatrixHint::Build(MapModel::hint_template(state->model, MapPreview::size()),
                                           static_cast<CBlockPar *>(nullptr));
    const int x = (g_ScreenX - hint->m_Width) / 2;
    const int y = (g_ScreenY - hint->m_Height) / 2;
    hint->Show(x, y);
    if (state->origin == Origin::Startup) {
        if (startup_hint) startup_hint->Release();
        startup_hint = hint;
        return;
    }
    auto &hints = g_MatrixMap->m_DialogModeHints;
    if (hints.empty()) {
        hints.push_back(hint);
    }
    else {
        hints[0]->Release();
        hints[0] = hint;
    }

}

MapModel::Language language() {
    return SettingsModel::detect_language(g_MatrixData->BlockGet(PAR_REPLACE)->ParGetNE(L"setup"));
}

void update(bool changed) {
    if (!changed) return;
    if (g_MatrixMap) CSound::Play(S_BCLICK);
    rebuild();
}

}  // namespace

bool available() { return Display::enabled(); }

bool active() {
    return state && current_hint() && (state->origin == Origin::Startup ||
           (g_MatrixMap->m_DialogModeName && wcscmp(g_MatrixMap->m_DialogModeName, dialog_name) == 0));
}

void open(Origin origin, const StandaloneSettings::Values &values) {
    if (!available() || (origin != Origin::Startup && !g_MatrixMap)) return;
    std::string diagnostic;
    const auto metadata = MapMetadata::load(diagnostic);
    if (!diagnostic.empty()) lgr.warning("Map metadata: {}")(diagnostic);
    const auto current = g_MatrixMap ? g_MatrixMap->MapName() : values.map;
    state.emplace(State{origin, values, MapModel::Window(MapCatalog::scan(), current, language(), metadata, values.battle_bonus)});
    state->actions = {Input::isVKeyPressed(VK_RETURN) || (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0,
                      Input::isVKeyPressed(VK_ESCAPE) || (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0};
    state->model.set_text_measure([](std::wstring_view line) { return Text::GetFont(g_D3DD, L"Font.2Small").CalcTextWidth(line); });
    lgr.notice("Map selector lists {} maps")(state->model.maps().size());
    if (g_MatrixMap) {
        g_MatrixMap->MouseCam(false);
        g_MatrixMap->LeaveDialogMode();
        g_MatrixMap->Pause(true);
        SETFLAG(g_MatrixMap->m_Flags, MMFLAG_DIALOG_MODE);
        g_MatrixMap->m_DialogModeName = dialog_name;
        CSound::Play(L"Formopen");
    }
    rebuild();
}

bool open_after_battle() {
    if (!available() || !g_MatrixMap) return false;
    open(Origin::BattleEnd, StandaloneSettings::current());
    return true;
}

void close(bool play) {
    if (!active()) return;
    if (play) {
        const auto *map = state->model.selected_map();
        if (!map) return;
        if (!MapCatalog::readable(map->path)) {
            lgr.error("The map {} cannot be loaded")(utils::from_wstring(map->path));
            state->model.set_error(MapModel::Error::Unreadable);
            rebuild();
            return;
        }
        std::string error;
        auto values = StandaloneSettings::load().values;
        values.map = map->path;
        values.battle_bonus = state->model.bonus();
        const bool startup = state->origin == Origin::Startup;
        if (!(startup ? StandaloneSettings::save(values, error) : MapSelection::choose(values, map->path, error))) {
            lgr.error("Cannot save CFG/standalone.txt: {}")(error);
            state->model.set_error(MapModel::Error::Save);
            rebuild();
            return;
        }
        if (!BattleLoading::show(g_Wnd, language() == MapModel::Language::Russian))
            lgr.warning("Cannot create the loading screen; continuing battle initialization");
        if (startup) {
            startup_choice = values;
            shutdown();
            SETFLAG(g_Flags, GFLAG_EXITLOOP);
            return;
        }
        shutdown();
        g_MatrixMap->LeaveDialogMode();
        SETFLAG(g_Flags, GFLAG_EXITLOOP);
        return;
    }
    const Origin origin = state->origin;
    shutdown();
    if (origin == Origin::Startup) return;
    if (origin == Origin::Menu) {
        g_MatrixMap->EnterDialogMode(TEMPLATE_DIALOG_MENU);
        return;
    }
    g_MatrixMap->LeaveDialogMode();
    SETFLAG(g_Flags, GFLAG_EXITLOOP);
}

void reset_input() {
    if (state) state->actions = {};
}

bool keyboard(uint8_t vk, bool down) {
    if (!active()) return false;
    if (!state->actions.accept(vk, down)) return true;
    auto &model = state->model;
    const int count = static_cast<int>(model.maps().size());
    switch (vk) {
        case VK_UP: update(model.move(-1)); break;
        case VK_DOWN: update(model.move(1)); break;
        case VK_PRIOR: update(model.scroll_description(-MapModel::Layout::description_rows)); break;
        case VK_NEXT: update(model.scroll_description(MapModel::Layout::description_rows)); break;
        case VK_LEFT: update(model.page(-1)); break;
        case VK_RIGHT: update(model.page(1)); break;
        case '1': case '2': case '3': update(model.select_bonus(vk - '1')); break;
        case VK_NUMPAD1: case VK_NUMPAD2: case VK_NUMPAD3: update(model.select_bonus(vk - VK_NUMPAD1)); break;
        case VK_HOME: update(model.move(-count)); break;
        case VK_END: update(model.move(count)); break;
        case VK_RETURN:
            lgr.notice("Starting selected map from Enter in the {} selector")(state->origin == Origin::Startup ? "startup" : "battle");
            close(true);
            break;
        case VK_ESCAPE: close(false); break;
        default: break;  // The battle stays paused; other keys are ignored while the window is open.
    }
    return true;
}

bool mouse_down(int x, int y, bool double_click) {
    if (!active()) return false;
    const CMatrixHint *hint = current_hint();
    const auto target = MapModel::hit(state->model, x - hint->m_PosX, y - hint->m_PosY);
    if (target.action) { close(target.action == 1); return true; }
    if (target.bonus >= 0) { update(state->model.select_bonus(target.bonus)); return true; }
    if (target.scroll) { update(state->model.scroll_description(target.scroll)); return true; }
    if (target.index >= 0) {
        update(state->model.select(target.index));
        if (double_click) close(true);
        return true;
    }
    if (target.page != 0) {
        update(state->model.page(target.page));
        return true;
    }
    return false;  // The hint buttons and the rest of the screen keep their handling.
}

void mouse_wheel(int steps, int x, int y) {
    if (!active()) return;
    const auto *hint = current_hint();
    x -= hint->m_PosX; y -= hint->m_PosY;
    if (x < 0 || x >= MapModel::Layout::width || y < 0 || y >= MapModel::Layout::content_height) return;
    if (x >= MapModel::Layout::preview_x && x < MapModel::Layout::preview_x + MapModel::Layout::detail_width &&
        y >= MapModel::Layout::description_y && y < MapModel::Layout::description_scroll_y + 20)
        update(state->model.scroll_description(-steps * 3));
    else if (x >= MapModel::Layout::list_x && x < MapModel::Layout::list_x + MapModel::Layout::list_width &&
             y >= MapModel::Layout::first_row_y && y < MapModel::Layout::pager_y)
        update(state->model.move(-steps));
}

void relayout() {
    if (active()) rebuild();
}

void shutdown() {
    if (startup_hint) startup_hint->Release();
    startup_hint = nullptr;
    MapPreview::release();
    state.reset();
}

std::optional<StandaloneSettings::Values> take_startup_choice() {
    auto choice = std::move(startup_choice);
    startup_choice.reset();
    return choice;
}

void customize_menu(std::wstring &menu_template) {
    if (!available()) return;
    utils::replace(menu_template, L"[help]", language() == MapModel::Language::Russian ? L"\u041d\u043e\u0432\u0430\u044f \u0431\u0438\u0442\u0432\u0430 (N)" : L"New battle (N)");
}

bool menu_keyboard(uint8_t vk, bool down) {
    // No new battle from the menu of a battle shared by several people.
    if (!available() || !g_MatrixMap || Commands::deferred() || !g_MatrixMap->m_DialogModeName ||
        wcscmp(g_MatrixMap->m_DialogModeName, TEMPLATE_DIALOG_MENU) != 0 || g_MatrixMap->m_DialogModeHints.size() != 1)
        return false;
    if (down && vk == 'N') { open(Origin::Menu, StandaloneSettings::current()); return true; }
    return false;
}

bool menu_mouse_down(int x, int y) {
    if (!available() || !g_MatrixMap || Commands::deferred() || !g_MatrixMap->m_DialogModeName ||
        wcscmp(g_MatrixMap->m_DialogModeName, TEMPLATE_DIALOG_MENU) != 0 || g_MatrixMap->m_DialogModeHints.size() != 1)
        return false;
    const auto *hint = g_MatrixMap->m_DialogModeHints[0];
    x -= hint->m_PosX; y -= hint->m_PosY;
    if (x >= 0 && x < 200 && y >= 245 && y < 275) {
        open(Origin::Menu, StandaloneSettings::current());
        return true;
    }
    return false;
}

}  // namespace MapWindow
