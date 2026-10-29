// MatrixGame - licensed under GPLv2 or any later version.
#include "Commands.hpp"

#include "MatrixMap.hpp"
#include "MatrixSide.hpp"
#include "Interface/CConstructor.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <format>
#include <sstream>

namespace Commands {
namespace {

bool g_deferred = false;
std::vector<Command> g_scheduled;  // sorted by time, stable for equal times
std::vector<Command> g_issued;     // from the interface, for the next step
std::function<void(const Command &)> g_observer;
std::function<void(Command)> g_router;
bool g_interface_input = true;

void execute(const Command &command) {
    if (g_observer)
        g_observer(command);
    for (int i = 0; i < g_MatrixMap->m_SideCnt; i++) {
        if (g_MatrixMap->m_Side[i].m_Id == command.side) {
            g_MatrixMap->m_Side[i].ExecuteCommand(command);
            return;
        }
    }
}

constexpr const char *g_names[] = {"",           "stop",         "move",         "attack",    "capture",
                                   "patrol",     "bomb",         "repair",       "auto-attack", "auto-capture",
                                   "auto-defence", "build-robot", "build-turret", "cancel-build", "maintenance",
                                   "surrender",  "drive",        "rejoin",       "turret-attack", "turret-dismantle"};

}  // namespace

void set_deferred(bool deferred) { g_deferred = deferred; }
bool deferred() { return g_deferred; }
bool interface_locked() { return g_deferred; }
void set_interface_input(bool live) { g_interface_input = live; }
bool interface_input() { return g_interface_input; }

void issue(Command command) {
    if (!g_deferred) {
        command.time = g_MatrixMap->GetTime();
        execute(command);
        return;
    }
    if (g_router) {
        g_router(std::move(command));
        return;
    }
    g_issued.push_back(std::move(command));
}

void schedule(Command command) {
    auto at = std::upper_bound(g_scheduled.begin(), g_scheduled.end(), command.time,
                               [](int time, const Command &c) { return time < c.time; });
    g_scheduled.insert(at, std::move(command));
}

void execute_due() {
    const int now = g_MatrixMap->GetTime();
    std::size_t n = 0;
    while (n < g_scheduled.size() && g_scheduled[n].time <= now)
        ++n;
    std::vector<Command> due(g_scheduled.begin(), g_scheduled.begin() + n);
    g_scheduled.erase(g_scheduled.begin(), g_scheduled.begin() + n);
    for (Command &c : g_issued) {
        c.time = now;
        due.push_back(std::move(c));
    }
    g_issued.clear();
    for (const Command &c : due)
        execute(c);
}

void set_observer(std::function<void(const Command &)> observer) { g_observer = std::move(observer); }
void set_router(std::function<void(Command)> router) { g_router = std::move(router); }

void reset() {
    g_scheduled.clear();
    g_issued.clear();
    g_observer = nullptr;
    g_router = nullptr;
    g_deferred = false;
    g_interface_input = true;
}

const char *type_name(Type type) {
    const int i = static_cast<int>(type);
    return i > 0 && i < int(std::size(g_names)) ? g_names[i] : "?";
}

std::string to_line(const Command &c) {
    std::string line = std::format("C {} {} {} {:08x} {:08x} {} {}", c.time, c.side, type_name(c.type),
                                   std::bit_cast<std::uint32_t>(c.x), std::bit_cast<std::uint32_t>(c.y), c.target,
                                   c.units.size());
    for (int id : c.units)
        line += std::format(" {}", id);
    line += std::format(" {}", c.params.size());
    for (int p : c.params)
        line += std::format(" {}", p);
    return line;
}

bool from_line(const std::string &line, Command &c) {
    std::istringstream in(line);
    std::string tag, name;
    std::uint32_t x = 0, y = 0;
    std::size_t units = 0, params = 0;
    if (!(in >> tag >> c.time >> c.side >> name) || tag != "C")
        return false;
    auto it = std::find(std::begin(g_names), std::end(g_names), name);
    if (it == std::begin(g_names) || it == std::end(g_names))
        return false;
    c.type = static_cast<Type>(it - std::begin(g_names));
    if (!(in >> std::hex >> x >> y >> std::dec >> c.target >> units))
        return false;
    c.x = std::bit_cast<float>(x);
    c.y = std::bit_cast<float>(y);
    c.units.resize(units);
    for (int &id : c.units)
        if (!(in >> id))
            return false;
    if (!(in >> params))
        return false;
    c.params.resize(params);
    for (int &p : c.params)
        if (!(in >> p))
            return false;
    return true;
}

CMatrixMapStatic *find(int sim_id) {
    if (sim_id <= 0)
        return nullptr;
    for (CMatrixMapStatic *ms = CMatrixMapStatic::GetFirstLogic(); ms; ms = ms->GetNextLogic())
        if (ms->GetSimId() == sim_id)
            return ms;
    return nullptr;
}

std::vector<int> design_params(const SSpecialBot &design, int count, int team) {
    std::vector<int> p{count,
                       team,
                       int(design.m_Chassis.m_nKind),
                       int(design.m_Armor.m_Unit.m_nKind),
                       int(design.m_Head.m_nKind)};
    for (int i = 0; i < MAX_WEAPON_CNT; i++) {
        p.push_back(int(design.m_Weapon[i].m_Unit.m_nKind));
        p.push_back(design.m_Weapon[i].m_Pos);
    }
    return p;
}

bool design_from_params(const std::vector<int> &p, SSpecialBot &design, int &count, int &team) {
    if (p.size() != std::size_t(5 + 2 * MAX_WEAPON_CNT))
        return false;
    ZeroMemory(&design, sizeof(SSpecialBot));
    count = p[0];
    team = p[1];
    design.m_Chassis.m_nKind = ERobotUnitKind(p[2]);
    design.m_Armor.m_Unit.m_nKind = ERobotUnitKind(p[3]);
    design.m_Head.m_nKind = ERobotUnitKind(p[4]);
    for (int i = 0; i < MAX_WEAPON_CNT; i++) {
        design.m_Weapon[i].m_Unit.m_nKind = ERobotUnitKind(p[5 + 2 * i]);
        design.m_Weapon[i].m_Pos = p[6 + 2 * i];
    }
    return true;
}

}  // namespace Commands
