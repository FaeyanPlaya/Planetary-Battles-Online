// MatrixGame - licensed under GPLv2 or any later version.
// Автоподрыв (Auto detonation), a battle mod: autoboom.dll in mods\autoboom. A robot of a person's side with a bomb
// sets it off as it dies (the original did it only for the AI's robots, and only with more enemy than own strength in
// the blast). Each robot keeps its own choice, on when built; a right click on the bomb order turns it for the
// selected bombers (an AutoBoom command) (AutoBoom). The tests build this file into themselves (PBO_MOD_STATIC).
#include "ModApi.hpp"
#include "ModHooks.hpp"

#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "MatrixSide.hpp"
#include "Commands.hpp"
#include "BattleNotice.hpp"
#include "SettingsModel.hpp"
#include "Interface/CInterface.h"
#include "Interface/MatrixHint.hpp"
#include "MatrixGame.h"

#include <format>

namespace AutoBoomMod {
namespace {

bool auto_boom(const CMatrixRobotAI &robot) { return robot.m_AutoBoom; }

bool toggle_auto_boom(CMatrixSideUnit &side) {
    // Off when every selected bomber has it on, else on for all of them.
    Commands::Command command;
    command.side = side.m_Id;
    command.type = Commands::Type::AutoBoom;
    bool all_on = true;
    if (side.GetCurGroup()) {
        for (CMatrixGroupObject *objs = side.GetCurGroup()->m_FirstObject; objs; objs = objs->m_NextObject) {
            CMatrixMapStatic *o = objs->GetObject();
            if (o && o->IsLiveRobot() && o->AsRobot()->HaveBomb()) {
                command.units.push_back(o->GetSimId());
                all_on = all_on && o->AsRobot()->m_AutoBoom;
            }
        }
    }
    if (command.units.empty())
        return false;
    command.params = {all_on ? 0 : 1};
    Commands::issue(command);
    return !all_on;
}

bool selected_auto_boom(CMatrixSideUnit &side) {
    if (side.GetCurGroup()) {
        for (CMatrixGroupObject *objs = side.GetCurGroup()->m_FirstObject; objs; objs = objs->m_NextObject) {
            CMatrixMapStatic *o = objs->GetObject();
            if (o && o->IsLiveRobot() && o->AsRobot()->HaveBomb() && !o->AsRobot()->m_AutoBoom)
                return false;
        }
    }
    return true;
}

// The bomb order's right click: the selected bombers' auto detonation turns; the hint shows it anew.
void bomb_right_click() {
    const bool on = g_MatrixMap->GetPlayerSide()->ToggleAutoBoom();
    const std::wstring setup = g_MatrixData->BlockGet(PAR_REPLACE)->ParGetNE(L"setup");
    const bool ru = SettingsModel::detect_language(setup) == SettingsModel::Language::Russian;
    BattleNotice::show_own(on ? (ru ? L"Автоподрыв включён" : L"Auto detonation on")
                              : (ru ? L"Автоподрыв выключен" : L"Auto detonation off"));
    if (g_IFaceList->m_CurrentHint && g_IFaceList->m_CurrentHintControlName == IF_ORDER_BOMB) {
        g_IFaceList->m_CurrentHint->Release();
        g_IFaceList->m_CurrentHint = NULL;
        g_IFaceList->m_CurrentHintControlName = L"";
    }
}

std::wstring bomb_hint(bool ru) {
    const wchar_t *text = g_MatrixMap->GetPlayerSide()->SelectedAutoBoom()
        ? (ru ? L"Взорвать бомбу. Автоподрыв при гибели: включён. Правый щелчок: выключить."
              : L"Set the bomb off. Auto detonation as it dies: on. Right click: turn off.")
        : (ru ? L"Взорвать бомбу. Автоподрыв при гибели: выключен. Правый щелчок: включить."
              : L"Set the bomb off. Auto detonation as it dies: off. Right click: turn on.");
    return std::format(L"0|_COLOR:FFCBCCCE|_FONT:Font.2Small|_WIDTH:175|_ALIGN:0:0|_TEXT:{}", text);
}

// An AutoBoom command: its robots of the side take its switch (on or off).
void auto_boom_command(CMatrixSideUnit &side, const Commands::Command &command) {
    if (command.params.size() != 1)
        return;
    for (int id : command.units) {
        CMatrixMapStatic *o = Commands::find(id);
        if (o && o->IsLiveRobot() && o->GetSide() == side.m_Id)
            o->AsRobot()->m_AutoBoom = command.params[0] != 0;
    }
}

}  // namespace

void init(ModApi::Context &) {
    auto &hooks = ModHooks::hooks();
    hooks.auto_boom = auto_boom;
    hooks.bomb_right_click = bomb_right_click;
    hooks.bomb_hint = bomb_hint;
    hooks.auto_boom_command = auto_boom_command;
    hooks.toggle_auto_boom = toggle_auto_boom;
    hooks.selected_auto_boom = selected_auto_boom;
}

}  // namespace AutoBoomMod

PBO_MOD_ENTRY("autoboom", AutoBoomMod::init)
