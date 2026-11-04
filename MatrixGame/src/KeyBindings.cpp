// MatrixGame - licensed under GPLv2 or any later version.
#include "KeyBindings.hpp"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace KeyBindings {
namespace {

struct KeyName {
    const wchar_t *name;
    int code;
};

// The game's own names (MatrixConfig had them for AssignKey).
constexpr KeyName key_names[] = {
    {L"KEY_LMB", VK_LBUTTON},
    {L"KEY_RMB", VK_RBUTTON},
    {L"KEY_ESC", VK_ESCAPE},
    {L"KEY_F1", 0x70},
    {L"KEY_F2", 0x71},
    {L"KEY_F3", 0x72},
    {L"KEY_F4", 0x73},
    {L"KEY_F5", 0x74},
    {L"KEY_F6", 0x75},
    {L"KEY_F7", 0x76},
    {L"KEY_F8", 0x77},
    {L"KEY_F9", 0x78},
    {L"KEY_F10", 0x79},
    {L"KEY_F11", 0x7A},
    {L"KEY_F12", 0x7B},
    {L"KEY_PAUSE", VK_PAUSE},
    {L"KEY_SLOCK", VK_SCROLL},
    {L"KEY_CLOCK", VK_CAPITAL},
    {L"KEY_NLOCK", VK_NUMLOCK},
    {L"KEY_TILDA", -1},             // ?
    {L"KEY_RSLASH", -1},            // ?
    {L"KEY_LSLASH", VK_OEM_5},      // ?
    {L"KEY_COMMA", VK_OEM_COMMA},   // ?
    {L"KEY_COLON", VK_OEM_PERIOD},  // ?
    {L"KEY_SEMICOLON", -1},         // ?
    {L"KEY_APOSTROF", -1},          // ?
    {L"KEY_LBRACKET", VK_OEM_4},    // ?
    {L"KEY_RBRACKET", VK_OEM_6},    // ?
    {L"KEY_MINUS", VK_OEM_MINUS},   // ?
    {L"KEY_EQUAL", VK_OEM_PLUS},    // ?
    {L"KEY_PADSLASH", VK_DIVIDE},
    {L"KEY_PADSTAR", VK_MULTIPLY},
    {L"KEY_PADMINUS", VK_SUBTRACT},
    {L"KEY_PADPLUS", VK_ADD},
    {L"KEY_PADENTER", -1},  // ?
    {L"KEY_PADDEL", VK_SEPARATOR},
    {L"KEY_PAD0", VK_NUMPAD0},
    {L"KEY_PAD1", VK_NUMPAD1},
    {L"KEY_PAD2", VK_NUMPAD2},
    {L"KEY_PAD3", VK_NUMPAD3},
    {L"KEY_PAD4", VK_NUMPAD4},
    {L"KEY_PAD5", VK_NUMPAD5},
    {L"KEY_PAD6", VK_NUMPAD6},
    {L"KEY_PAD7", VK_NUMPAD7},
    {L"KEY_PAD8", VK_NUMPAD8},
    {L"KEY_PAD9", VK_NUMPAD9},
    {L"KEY_1", 0x31},
    {L"KEY_2", 0x32},
    {L"KEY_3", 0x33},
    {L"KEY_4", 0x34},
    {L"KEY_5", 0x35},
    {L"KEY_6", 0x36},
    {L"KEY_7", 0x37},
    {L"KEY_8", 0x38},
    {L"KEY_9", 0x39},
    {L"KEY_0", 0x30},
    {L"KEY_LEFT", VK_LEFT},
    {L"KEY_RIGHT", VK_RIGHT},
    {L"KEY_UP", VK_UP},
    {L"KEY_DOWN", VK_DOWN},
    {L"KEY_BACKSPACE", VK_BACK},
    {L"KEY_TAB", VK_TAB},
    {L"KEY_ENTER", VK_RETURN},
    {L"KEY_SPACE", VK_SPACE},
    {L"KEY_INSERT", VK_INSERT},
    {L"KEY_DELETE", VK_DELETE},
    {L"KEY_HOME", VK_HOME},
    {L"KEY_END", VK_END},
    {L"KEY_PGUP", VK_PRIOR},
    {L"KEY_PGDN", VK_NEXT},
    {L"KEY_SHIFT", VK_SHIFT},
    {L"KEY_LSHIFT", VK_LSHIFT},
    {L"KEY_RSHIFT", VK_RSHIFT},
    {L"KEY_ALT", VK_MENU},
    {L"KEY_LALT", VK_LMENU},
    {L"KEY_RALT", VK_RMENU},
    {L"KEY_CTRL", VK_CONTROL},
    {L"KEY_LCTRL", VK_LCONTROL},
    {L"KEY_RCTRL", VK_RCONTROL},
    {L"KEY_LWIN", VK_LWIN},
    {L"KEY_RWIN", VK_RWIN},
    {L"KEY_MENU", VK_APPS},
    {L"KEY_Q", 0x51},
    {L"KEY_W", 0x57},
    {L"KEY_E", 0x45},
    {L"KEY_R", 0x52},
    {L"KEY_T", 0x54},
    {L"KEY_Y", 0x59},
    {L"KEY_U", 0x55},
    {L"KEY_I", 0x49},
    {L"KEY_O", 0x4F},
    {L"KEY_P", 0x50},
    {L"KEY_A", 0x41},
    {L"KEY_S", 0x53},
    {L"KEY_D", 0x44},
    {L"KEY_F", 0x46},
    {L"KEY_G", 0x47},
    {L"KEY_H", 0x48},
    {L"KEY_J", 0x4A},
    {L"KEY_K", 0x4B},
    {L"KEY_L", 0x4C},
    {L"KEY_Z", 0x5A},
    {L"KEY_X", 0x58},
    {L"KEY_C", 0x43},
    {L"KEY_V", 0x56},
    {L"KEY_B", 0x42},
    {L"KEY_N", 0x4E},
    {L"KEY_M", 0x4D},
};

// What the keys page shows instead of a name for keys with a character of their own.
constexpr KeyName key_characters[] = {
    {L"`", VK_OEM_3}, {L"\\", VK_OEM_5}, {L"/", VK_OEM_2}, {L",", VK_OEM_COMMA}, {L".", VK_OEM_PERIOD},
    {L";", VK_OEM_1}, {L"'", VK_OEM_7}, {L"[", VK_OEM_4}, {L"]", VK_OEM_6}, {L"-", VK_OEM_MINUS},
    {L"=", VK_OEM_PLUS},
};

using G = Group;
// Not here: the mouse's fire and auto (KA_FIRE, KA_AUTO) and the modifiers (KA_SHIFT, KA_CTRL).
constexpr Action action_list[] = {
    {KA_SCROLL_UP, "ScrollUp", L"Forward", L"Вперёд", G::Camera},
    {KA_SCROLL_DOWN, "ScrollDown", L"Back", L"Назад", G::Camera},
    {KA_SCROLL_LEFT, "ScrollLeft", L"Left", L"Влево", G::Camera},
    {KA_SCROLL_RIGHT, "ScrollRight", L"Right", L"Вправо", G::Camera},
    {KA_SCROLL_UP_ALT, "ScrollUpAlt", L"Forward (2)", L"Вперёд (2)", G::Camera},
    {KA_SCROLL_DOWN_ALT, "ScrollDownAlt", L"Back (2)", L"Назад (2)", G::Camera},
    {KA_SCROLL_LEFT_ALT, "ScrollLeftAlt", L"Left (2)", L"Влево (2)", G::Camera},
    {KA_SCROLL_RIGHT_ALT, "ScrollRightAlt", L"Right (2)", L"Вправо (2)", G::Camera},
    {KA_ROTATE_LEFT, "RotateLeft", L"Turn left", L"Поворот влево", G::Camera},
    {KA_ROTATE_RIGHT, "RotateRight", L"Turn right", L"Поворот вправо", G::Camera},
    {KA_ROTATE_LEFT_ALT, "RotateLeftAlt", L"Turn left (2)", L"Поворот влево (2)", G::Camera},
    {KA_ROTATE_RIGHT_ALT, "RotateRightAlt", L"Turn right (2)", L"Поворот вправо (2)", G::Camera},
    {KA_ROTATE_UP, "RotateUp", L"Tilt up", L"Наклон вверх", G::Camera},
    {KA_ROTATE_DOWN, "RotateDown", L"Tilt down", L"Наклон вниз", G::Camera},
    {KA_CAM_SETDEFAULT, "CameraDefault", L"Camera reset", L"Камера как была", G::Camera},
    {KA_MINIMAP_ZOOMIN, "MinimapIn", L"Minimap in", L"Миникарта ближе", G::Camera},
    {KA_MINIMAP_ZOOMOUT, "MinimapOut", L"Minimap out", L"Миникарта дальше", G::Camera},

    {KA_UNIT_ENTER, "Enter", L"Enter, leave", L"Войти, выйти", G::Driving},
    {KA_UNIT_ENTER_ALT, "EnterAlt", L"Enter, leave (2)", L"Войти, выйти (2)", G::Driving},
    {KA_UNIT_FORWARD, "Forward", L"Forward", L"Вперёд", G::Driving},
    {KA_UNIT_BACKWARD, "Backward", L"Back", L"Назад", G::Driving},
    {KA_UNIT_LEFT, "Left", L"Turn left", L"Поворот влево", G::Driving},
    {KA_UNIT_RIGHT, "Right", L"Turn right", L"Поворот вправо", G::Driving},
    {KA_UNIT_FORWARD_ALT, "ForwardAlt", L"Forward (2)", L"Вперёд (2)", G::Driving},
    {KA_UNIT_BACKWARD_ALT, "BackwardAlt", L"Back (2)", L"Назад (2)", G::Driving},
    {KA_UNIT_LEFT_ALT, "LeftAlt", L"Turn left (2)", L"Поворот влево (2)", G::Driving},
    {KA_UNIT_RIGHT_ALT, "RightAlt", L"Turn right (2)", L"Поворот вправо (2)", G::Driving},
    {KA_UNIT_BOOM, "Explode", L"Explode", L"Взорвать", G::Driving},
    {KA_UNIT_STRAFE_LEFT, "StrafeLeft", L"Sideways left", L"Стрейф влево", G::Driving},
    {KA_UNIT_STRAFE_RIGHT, "StrafeRight", L"Sideways right", L"Стрейф вправо", G::Driving},
    {KA_UNIT_BOOM_EXTRAS, "ExplodeExtras", L"Explode (mod)", L"Взорвать (мод)", G::Driving},
    {KA_UNIT_REAR_VIEW, "RearView", L"Look back", L"Вид назад", G::Driving},
    {KA_UNIT_SLOT1, "Slot1", L"Weapon 1", L"Оружие 1", G::Driving},
    {KA_UNIT_SLOT2, "Slot2", L"Weapon 2", L"Оружие 2", G::Driving},
    {KA_UNIT_SLOT3, "Slot3", L"Weapon 3", L"Оружие 3", G::Driving},
    {KA_UNIT_SLOT4, "Slot4", L"Weapon 4", L"Оружие 4", G::Driving},
    {KA_UNIT_SLOT5, "Slot5", L"Weapon 5", L"Оружие 5", G::Driving},

    {KA_ORDER_MOVE, "Move", L"Move", L"Двигаться", G::Orders},
    {KA_ORDER_STOP, "Stop", L"Stop", L"Стоять", G::Orders},
    {KA_ORDER_ATTACK, "Attack", L"Attack", L"Атаковать", G::Orders},
    {KA_ORDER_CAPTURE, "Capture", L"Capture", L"Захватить", G::Orders},
    {KA_ORDER_PATROL, "Patrol", L"Patrol", L"Патрулировать", G::Orders},
    {KA_ORDER_REPAIR, "Repair", L"Repair", L"Чинить", G::Orders},
    {KA_ORDER_EXPLODE, "OrderExplode", L"Explode", L"Взорвать", G::Orders},
    {KA_ORDER_CANCEL, "Cancel", L"Cancel", L"Отменить", G::Orders},
    {KA_AUTOORDER_ATTACK, "AutoAttack", L"Auto: attack", L"Авто: атака", G::Orders},
    {KA_AUTOORDER_CAPTURE, "AutoCapture", L"Auto: capture", L"Авто: захват", G::Orders},
    {KA_AUTOORDER_DEFEND, "AutoDefend", L"Auto: defend", L"Авто: защита", G::Orders},
    {KA_ORDER_ROBOT_SWITCH1, "PreviousRobot", L"Previous robot", L"Прежний робот", G::Orders},
    {KA_ORDER_ROBOT_SWITCH2, "NextRobot", L"Next robot", L"Следующий робот", G::Orders},

    {KA_BUILD_ROBOT, "BuildRobot", L"Build a robot", L"Строить робота", G::Building},
    {KA_BUILD_TURRET, "BuildTurret", L"Build a turret", L"Строить турель", G::Building},
    {KA_BUILD_HELP, "CallHelp", L"Reinforcements", L"Подкрепление", G::Building},
    {KA_TURRET_CANNON, "TurretCannon", L"Turret", L"Турель", G::Building},
    {KA_TURRET_GUN, "TurretGun", L"Gun", L"Пушка", G::Building},
    {KA_TURRET_LASER, "TurretLaser", L"Laser", L"Лазер", G::Building},
    {KA_TURRET_ROCKET, "TurretRocket", L"Rocket launcher", L"Ракетница", G::Building},
};

std::vector<ModAction> g_mod_actions;
Keys g_base = defaults();
std::map<std::string, int> g_mod_keys;

std::wstring widen(const std::string &text) { return std::wstring(text.begin(), text.end()); }

// A mod's key id: <mod>.<Id>, letters, digits, _ and - only.
bool mod_id(const std::string &id) {
    const auto dot = id.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == id.size()) return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
               c == '.';
    });
}

}  // namespace

int code(const std::wstring &name) {
    for (const auto &key : key_names)
        if (name == key.name) return key.code;
    if (!name.empty() && name.size() <= 3 && std::all_of(name.begin(), name.end(), [](wchar_t c) { return std::iswdigit(c); })) {
        const int number = std::stoi(name);
        if (number > 0 && number < 256) return number;
    }
    return -1;
}

std::wstring name(int code) {
    if (code <= 0) return {};
    for (const auto &key : key_names)
        if (key.code == code) return key.name;
    return code > 0 && code < 256 ? std::to_wstring(code) : std::wstring();
}

std::wstring shown(int code) {
    for (const auto &key : key_characters)
        if (key.code == code) return key.name;
    const std::wstring full = name(code);
    if (full.empty()) return L"-";
    return full.rfind(L"KEY_", 0) == 0 ? full.substr(4) : L"#" + full;
}

void defaults(int *keys) {
    for (int i = 0; i < KA_LAST; ++i) keys[i] = -1;
    keys[KA_ROTATE_LEFT] = VK_HOME;
    keys[KA_ROTATE_RIGHT] = VK_END;
    keys[KA_ROTATE_UP] = VK_PRIOR;
    keys[KA_ROTATE_DOWN] = VK_NEXT;

    keys[KA_SCROLL_UP_ALT] = VK_UP;        // 0x57;
    keys[KA_SCROLL_DOWN_ALT] = VK_DOWN;    // 0x53;
    keys[KA_SCROLL_LEFT_ALT] = VK_LEFT;    // 0x41;
    keys[KA_SCROLL_RIGHT_ALT] = VK_RIGHT;  // 0x44;

    keys[KA_SCROLL_UP] = VK_UP;
    keys[KA_SCROLL_DOWN] = VK_DOWN;
    keys[KA_SCROLL_LEFT] = VK_LEFT;
    keys[KA_SCROLL_RIGHT] = VK_RIGHT;

    keys[KA_UNIT_FORWARD] = 0x57; // W
    keys[KA_UNIT_BACKWARD] = 0x53; // S
    keys[KA_UNIT_LEFT] = 0x41; // A
    keys[KA_UNIT_RIGHT] = 0x44; // D

    keys[KA_UNIT_FORWARD_ALT] = VK_UP;
    keys[KA_UNIT_BACKWARD_ALT] = VK_DOWN;
    keys[KA_UNIT_LEFT_ALT] = VK_LEFT;
    keys[KA_UNIT_RIGHT_ALT] = VK_RIGHT;

    keys[KA_FIRE] = VK_LBUTTON;
    keys[KA_AUTO] = VK_RBUTTON;

    keys[KA_SHIFT] = VK_SHIFT;
    keys[KA_CTRL] = VK_CONTROL;

    keys[KA_ROTATE_LEFT_ALT] = VK_OEM_4;  // [{ key
    keys[KA_ROTATE_RIGHT_ALT] = VK_OEM_6; // ]} key

    keys[KA_MINIMAP_ZOOMIN] = VK_OEM_PLUS;   // + key
    keys[KA_MINIMAP_ZOOMOUT] = VK_OEM_MINUS; // - key

    keys[KA_CAM_SETDEFAULT] = VK_OEM_5; // \| key

    keys[KA_AUTOORDER_CAPTURE] = 0x43;  // C
    keys[KA_AUTOORDER_ATTACK] = 0x55;   // U
    keys[KA_AUTOORDER_DEFEND] = 0x44;   // D

    keys[KA_ORDER_MOVE] = 0x4D;     // M
    keys[KA_ORDER_STOP] = 0x53;     // S
    keys[KA_ORDER_CAPTURE] = 0x4B;  // K
    keys[KA_ORDER_PATROL] = 0x50;   // P
    keys[KA_ORDER_EXPLODE] = 0x45;  // E
    keys[KA_ORDER_REPAIR] = 0x52;   // R
    keys[KA_ORDER_ATTACK] = 0x41;   // A
    keys[KA_ORDER_CANCEL] = 0x58;   // X
    keys[KA_ORDER_ROBOT_SWITCH1] = VK_OEM_COMMA;   // , key
    keys[KA_ORDER_ROBOT_SWITCH2] = VK_OEM_PERIOD;  // . key

    keys[KA_UNIT_BOOM] = 0x45;           // E
    keys[KA_UNIT_ENTER] = VK_RETURN;     // (! if not dialog mode)
    keys[KA_UNIT_ENTER_ALT] = VK_SPACE;  // (! if not dialog mode)

    keys[KA_BUILD_ROBOT] = 0x42;   // B
    keys[KA_BUILD_TURRET] = 0x54;  // T
    keys[KA_BUILD_HELP] = 0x48;    // H

    keys[KA_TURRET_CANNON] = 0x43; // C
    keys[KA_TURRET_GUN] = 0x47;    // G
    keys[KA_TURRET_LASER] = 0x4C;  // L
    keys[KA_TURRET_ROCKET] = 0x52; // R

    keys[KA_UNIT_STRAFE_LEFT] = 0x51;   // Q
    keys[KA_UNIT_STRAFE_RIGHT] = 0x45;  // E
    keys[KA_UNIT_BOOM_EXTRAS] = 0x46;   // F
    keys[KA_UNIT_REAR_VIEW] = 0x58;     // X

    keys[KA_UNIT_SLOT1] = 0x31;  // 1
    keys[KA_UNIT_SLOT2] = 0x32;
    keys[KA_UNIT_SLOT3] = 0x33;
    keys[KA_UNIT_SLOT4] = 0x34;
    keys[KA_UNIT_SLOT5] = 0x35;
}

Keys defaults() {
    Keys keys;
    defaults(keys.data());
    for (const auto &action : g_mod_actions) keys.mods[action.id] = action.key;
    return keys;
}

int Keys::mod(const std::string &id) const {
    const auto found = mods.find(id);
    return found == mods.end() ? -1 : found->second;
}

const Keys &base() { return g_base; }

void set_base(const int *keys) { std::copy(keys, keys + KA_LAST, g_base.begin()); }

std::span<const Action> actions() { return action_list; }

Group group_of(std::wstring_view name) {
    constexpr std::pair<const wchar_t *, Group> groups[] = {
        {L"camera", Group::Camera}, {L"driving", Group::Driving}, {L"orders", Group::Orders}, {L"building", Group::Building}};
    for (const auto &[text, group] : groups)
        if (name.size() == std::wcslen(text) && _wcsnicmp(name.data(), text, name.size()) == 0) return group;
    return Group::Mods;
}

void set_mod_actions(std::vector<ModAction> actions) {
    g_mod_actions = std::move(actions);
    g_base.mods.clear();
    for (const auto &action : g_mod_actions) g_base.mods[action.id] = action.key;
}

std::span<const ModAction> mod_actions() { return g_mod_actions; }

void set_mod_keys(const std::map<std::string, int> &keys) { g_mod_keys = keys; }

int mod_key(std::string_view id) {
    const std::string key(id);
    if (const auto found = g_mod_keys.find(key); found != g_mod_keys.end()) return found->second;
    return g_base.mod(key);
}

const wchar_t *group_name(Group group, bool russian) {
    switch (group) {
        case Group::Camera: return russian ? L"Камера" : L"Camera";
        case Group::Driving: return russian ? L"Ручное управление" : L"Manual control";
        case Group::Orders: return russian ? L"Приказы" : L"Orders";
        case Group::Building: return russian ? L"Строительство" : L"Building";
        case Group::Mods: return russian ? L"Моды" : L"Mods";
    }
    return L"";
}

Keys load(const std::wstring &path, const Keys &over, std::vector<std::string> *invalid) {
    Keys keys = over;
    std::ifstream input{std::filesystem::path(path)};
    for (std::string line; std::getline(input, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == ';') continue;
        const auto equal = line.find('=');
        std::string id = line.substr(0, equal);
        if (id == "SelectAll") id = "interface.SelectAll";  // the engine's until the interface mod took it (2026-11)
        const Action *found = nullptr;
        for (const auto &action : action_list)
            if (id == action.id) found = &action;
        const int key = equal == std::string::npos ? -1 : code(widen(line.substr(equal + 1)));
        if ((!found && !mod_id(id)) || key < 0) {
            if (invalid) invalid->push_back(id);
            continue;
        }
        if (found) keys[found->action] = key;
        else keys.mods[id] = key;
    }
    return keys;
}

bool save(const std::wstring &path, const Keys &keys, const Keys &over, std::string &error) {
    std::ostringstream text;
    text << "; Planetary Battles Online: this person's keys (the settings' Keys page), the ones that differ from the "
            "game's\n";
    for (const auto &action : action_list) {
        if (keys[action.action] == over[action.action]) continue;
        const std::wstring key = name(keys[action.action]);
        text << action.id << '=' << std::string(key.begin(), key.end()) << '\n';
    }
    for (const auto &[id, code] : keys.mods) {
        if (code == over.mod(id) || code < 0) continue;
        const std::wstring key = name(code);
        text << id << '=' << std::string(key.begin(), key.end()) << '\n';
    }
    const std::filesystem::path target(path);
    auto temporary = target;
    temporary += L".tmp";
    std::error_code failed;
    if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), failed);
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        const std::string bytes = text.str();
        stream.write(bytes.data(), std::streamsize(bytes.size()));
        if (!stream.good()) {
            error = "cannot write a temporary file";
            return false;
        }
    }
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "cannot replace the file";
        std::filesystem::remove(temporary, failed);
        return false;
    }
    return true;
}

}  // namespace KeyBindings
